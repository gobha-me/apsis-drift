#include "apsis_drift/origin_boarding_source_endpoint_load.hpp"
#include "origin_boarding_source_endpoint_load_internal.hpp"
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
using Bounds = BoardingPlantedLegScalarBounds;
using Load = BoardingSourceEndpointLoadDiagnostic;
using Quad = BoardingSourceEndpointLoadQuadMathEvidence;
using Math = BoardingSourceEndpointLoadPressureMathEvidence;
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
  if (!value) throw std::runtime_error("Required fixed-load API refused");
  return std::move(*value);
}
auto bits(double a, double b) -> bool {
  return std::bit_cast<std::uint64_t>(a) == std::bit_cast<std::uint64_t>(b);
}
struct Wide {
  long double x{}, y{}, z{};
};
auto add(Wide a, Wide b) -> Wide {
  return {a.x + b.x, a.y + b.y, a.z + b.z};
}
auto sub(Wide a, Wide b) -> Wide {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}
auto scale(Wide a, long double s) -> Wide {
  return {a.x * s, a.y * s, a.z * s};
}
auto dot(Wide a, Wide b) -> long double {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}
auto component(Wide p, std::size_t axis) -> long double {
  return axis == 0 ? p.x : axis == 1 ? p.y : p.z;
}
auto component(RigidVector3 p, std::size_t axis) -> double {
  return axis == 0 ? p.x : axis == 1 ? p.y : p.z;
}
auto corroboration_error(long double value) -> long double {
  // Independent LD evaluation uncertainty, never a geometric admission epsilon.
  return 65536 * std::numeric_limits<long double>::epsilon() *
         std::max(1.L, std::abs(value));
}
auto contains(Bounds b, long double x) -> void {
  check(std::isfinite(b.lower) && std::isfinite(b.upper) &&
            b.lower <= b.upper &&
            static_cast<long double>(b.lower) <= x + corroboration_error(x) &&
            x - corroboration_error(x) <= static_cast<long double>(b.upper),
        "Independent exact-expression value belongs to outward bounds");
}
auto contains(BoardingFootSiteScalarBounds b, long double x) -> void {
  check(b.supported, "Observed scalar bound retains supported state");
  if (b.supported) contains(Bounds{b.lower, b.upper}, x);
}
auto point_contains(const BoardingPlantedLegPointBounds& b, Wide p) -> void {
  for (std::size_t axis = 0; axis < 3; ++axis)
    contains(Bounds{component(b.lower, axis), component(b.upper, axis)},
             component(p, axis));
}
auto point_contains(const std::array<RigidVector3, 2>& b, Wide p) -> void {
  for (std::size_t axis = 0; axis < 3; ++axis)
    contains(Bounds{component(b[0], axis), component(b[1], axis)},
             component(p, axis));
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
  std::array<Wide, 18> points{};
  std::array<Wide, 15> mass_points{};
  std::array<Wide, 2> centers{}, pressures{};
  Wide com{}, barycenter{}, delta{};
};
auto oracle() -> Oracle {
  Oracle out;
  const Wide root{static_cast<long double>(.16), 0,
                  static_cast<long double>(-.55)};
  out.points[0] = root;
  out.points[1] = add(root, {0, static_cast<long double>(.3495), 0});
  out.points[2] = add(root, {0, static_cast<long double>(.70237), 0});
  out.points[3] = add(root, {0, static_cast<long double>(.65237), 0});
  for (std::size_t foot = 0; foot < 2; ++foot) {
    const auto base = foot == 0 ? std::size_t{4} : std::size_t{11};
    const auto x = static_cast<long double>(.16) +
                   (foot == 0 ? -static_cast<long double>(.14)
                              : static_cast<long double>(.14));
    const auto plane = static_cast<long double>(
        foot == 0 ? kCabinCorridorFloorMetres : -160000.0 * 1e-6);
    const Wide hip{x, 0, static_cast<long double>(-.55)},
        ankle{x,
              plane + static_cast<long double>(.1) -
                  static_cast<long double>(.847),
              static_cast<long double>(foot == 0 ? -.5 : -.8)};
    const auto d = sub(ankle, hip);
    const auto length = std::sqrt(dot(d, d)),
               L1 = static_cast<long double>(.47285),
               L2 = static_cast<long double>(.47478);
    // Circle intersection: independent unfactored axial distance and height.
    // This does not call a producer leg/interval helper or reconstruct angles.
    const auto along = (L1 * L1 - L2 * L2 + length * length) / (2 * length),
               height = std::sqrt(L1 * L1 - along * along);
    const Wide perpendicular{0, -d.z / length, d.y / length};
    const auto knee =
        add(hip, add(scale(d, along / length), scale(perpendicular, height)));
    out.points[base] = hip;
    out.points[base + 1] = knee;
    out.points[base + 2] = ankle;
    out.points[base + 3] = {x,
                            plane + static_cast<long double>(.05) -
                                static_cast<long double>(.847),
                            ankle.z};
    out.centers[foot] = {x, plane + static_cast<long double>(.05), ankle.z};
    const auto shoulder =
        add(root, {foot == 0 ? -static_cast<long double>(.20265)
                             : static_cast<long double>(.20265),
                   static_cast<long double>(.579), 0});
    const auto c45 = std::sqrt(.5L);
    out.points[base + 4] = shoulder;
    out.points[base + 5] =
        add(shoulder, {0, -static_cast<long double>(.35898) * c45,
                       -static_cast<long double>(.35898) * c45});
    out.points[base + 6] =
        add(out.points[base + 5], {0, static_cast<long double>(.386), 0});
    const auto upper = sub(knee, hip), lower = sub(ankle, knee);
    check(std::abs(dot(upper, upper) - L1 * L1) <=
                  corroboration_error(L1 * L1) &&
              std::abs(dot(lower, lower) - L2 * L2) <=
                  corroboration_error(L2 * L2),
          "Independent circle-intersection knee preserves both nominal bones");
    check(bits(static_cast<double>(hip.x), static_cast<double>(ankle.x)) &&
              knee.x == hip.x,
          "Matched X expression makes both original leg mass axes structurally "
          "upright in X");
  }
  std::uint32_t total{};
  Wide weighted{};
  for (std::size_t i = 0; i < masses.size(); ++i) {
    const auto& m = masses[i];
    total += m.weight;
    out.mass_points[i] =
        scale(add(out.points[m.first], out.points[m.second]), .5L);
    weighted = add(weighted, scale(out.mass_points[i], m.weight));
  }
  check(total == 1200 &&
            masses[0].weight + masses[1].weight + masses[2].weight == 780,
        "All fifteen independent integer masses total1200 with central780");
  for (std::size_t start : std::array<std::size_t, 2>{3, 9})
    check(masses[start].weight + masses[start + 1].weight +
                      masses[start + 2].weight ==
                  180 &&
              masses[start + 3].weight + masses[start + 4].weight +
                      masses[start + 5].weight ==
                  30,
          "Each leg180 and arm30 independently establish paired mass "
          "cancellation");
  out.com = scale(weighted, 1.L / 1200);
  // Canonical COM gains exactly one common Y; the XZ pressure formula is
  // unchanged.
  out.com.y += static_cast<long double>(.847);
  out.barycenter = scale(add(out.centers[0], out.centers[1]), .5L);
  out.delta = {0, 0, out.com.z - out.barycenter.z};
  for (std::size_t i = 0; i < 2; ++i) {
    out.pressures[i] = add(out.centers[i], out.delta);
    out.pressures[i].y = static_cast<long double>(
        i == 0 ? kCabinCorridorFloorMetres : -160000.0 * 1e-6);
  }
  check(std::abs(out.com.x - root.x) <= corroboration_error(root.x) &&
            out.barycenter.x == root.x,
        "Independent mass sum corroborates exact paired COM-X/barycenter "
        "binding");
  const auto mean = scale(add(out.pressures[0], out.pressures[1]), .5L);
  check(std::abs(mean.x - out.com.x) <= corroboration_error(out.com.x) &&
            std::abs(mean.z - out.com.z) <= corroboration_error(out.com.z),
        "Shared pressure offsets balance the projected resultant at actual "
        "full-mass COM");
  check(out.pressures[0].y != out.pressures[1].y &&
            std::abs(mean.y - out.com.y) > .1L,
        "Unequal source heights do not masquerade as a full3D COM barycenter");
  const auto moment = add(Wide{-(out.pressures[0].z - out.com.z) / 2, 0,
                               (out.pressures[0].x - out.com.x) / 2},
                          Wide{-(out.pressures[1].z - out.com.z) / 2, 0,
                               (out.pressures[1].x - out.com.x) / 2});
  check(std::abs(moment.x) <= corroboration_error(moment.x) && moment.y == 0 &&
            std::abs(moment.z) <= corroboration_error(moment.z) &&
            .5L + .5L - 1.L == 0,
        "Independent vertical cross products and exact half fractions give "
        "zero force/moment");
  return out;
}
struct XZ {
  long double x{}, z{};
};
auto polygon(const BoardingBootSourcePartition& part) -> std::array<XZ, 4> {
  std::array<XZ, 4> result{};
  for (std::size_t i = 0; i < 4; ++i)
    result[i] = {part.perimeter_metres[i].x, part.perimeter_metres[i].z};
  return result;
}
auto sole(Wide center) -> std::array<XZ, 4> {
  const auto hx = static_cast<long double>(.06),
             hz = static_cast<long double>(.14);
  return {{{center.x + hx, center.z - hz},
           {center.x - hx, center.z - hz},
           {center.x - hx, center.z + hz},
           {center.x + hx, center.z + hz}}};
}
auto signed_side(XZ a, XZ b, XZ p) -> long double {
  return (b.z - a.z) * (p.x - a.x) - (b.x - a.x) * (p.z - a.z);
}
auto edge_length_squared(XZ a, XZ b) -> long double {
  const auto dx = b.x - a.x, dz = b.z - a.z;
  return dx * dx + dz * dz;
}
auto inspect_edges(const std::array<BoardingFootSiteEdgeEvidence, 4>& edges,
                   const std::array<XZ, 4>& corners, XZ pressure) -> void {
  const auto required =
      static_cast<long double>(.020) + static_cast<long double>(.010);
  for (std::size_t i = 0; i < 4; ++i) {
    const auto& e = edges[i];
    const auto side = signed_side(corners[i], corners[(i + 1) % 4], pressure),
               len2 = edge_length_squared(corners[i], corners[(i + 1) % 4]),
               gap = side * side - required * required * len2;
    if (e.signed_side.supported && e.edge_length_squared.supported &&
        e.squared_margin_gap.supported) {
      contains(e.signed_side, side);
      contains(e.edge_length_squared, len2);
      contains(e.squared_margin_gap, gap);
    }
    if (e.disk_contained) {
      check(e.signed_side.supported && e.edge_length_squared.supported &&
                e.squared_margin_gap.supported && e.signed_side.lower > 0 &&
                e.edge_length_squared.lower > 0 &&
                e.squared_margin_gap.lower >= 0,
            "Every granted edge has supported positive side/length and outward "
            "nonnegative gap");
      check(side > 0 && gap >= -corroboration_error(gap),
            "Independent finite edge corroborates the claimed signed disk "
            "margin");
    }
  }
}
auto inspect_quad(const Quad& q, const std::array<RigidVector3, 4>& corners,
                  double plane) -> void {
  long double minimum_len = std::numeric_limits<long double>::infinity(),
              minimum_side = minimum_len;
  bool horizontal = true;
  for (std::size_t i = 0; i < 4; ++i) {
    horizontal = horizontal && corners[i].y == plane;
    const XZ a{corners[i].x, corners[i].z},
        b{corners[(i + 1) % 4].x, corners[(i + 1) % 4].z};
    minimum_len = std::min(minimum_len, edge_length_squared(a, b));
    for (std::size_t offset : std::array<std::size_t, 2>{2, 3}) {
      const auto& p = corners[(i + offset) % 4];
      minimum_side = std::min(minimum_side, signed_side(a, b, {p.x, p.z}));
    }
  }
  if (q.arithmetic_supported) {
    check(q.checked_edges <= 4 && q.checked_side_signs <= 8,
          "Quad guard records only finite registered perimeter attempts");
    if (q.checked_edges == 4 && q.minimum_edge_squared.supported)
      contains(q.minimum_edge_squared, minimum_len);
    if (q.checked_side_signs == 8 && q.minimum_signed_side.supported)
      contains(q.minimum_signed_side, minimum_side);
  }
  if (q.valid)
    check(q.arithmetic_supported && q.horizontal && q.upward && q.convex &&
              q.nondegenerate && horizontal && minimum_len > 0 &&
              minimum_side > 0 && q.checked_edges == 4 &&
              q.checked_side_signs == 8,
          "A valid quad requires all eight upward convexity signs and all four "
          "nonzero edges");
}
auto triangle_binding(const BoardingBootSourcePartition& part) -> void {
  std::array<unsigned, 4> incidence{};
  for (const auto& face : part.faces) {
    std::array<bool, 4> seen{};
    for (const auto& p : face.points_current_metres) {
      const auto it = std::ranges::find(part.perimeter_metres, p);
      check(
          it != part.perimeter_metres.end() && p.y == part.plane_metres,
          "Source triangle binds a genuine exact horizontal perimeter corner");
      if (it == part.perimeter_metres.end()) continue;
      const auto index =
          static_cast<std::size_t>(it - part.perimeter_metres.begin());
      check(!seen[index], "Source triangle corners are distinct");
      seen[index] = true;
      ++incidence[index];
    }
    const auto& a = face.points_current_metres[0];
    const auto& b = face.points_current_metres[1];
    const auto& c = face.points_current_metres[2];
    check(signed_side({a.x, a.z}, {b.x, b.z}, {c.x, c.z}) > 0,
          "Actual paired triangle has upward geometric winding without "
          "trusting shading normals");
  }
  std::array<std::size_t, 2> common{};
  std::size_t count{};
  for (std::size_t i = 0; i < 4; ++i) {
    check(incidence[i] == 1 || incidence[i] == 2,
          "Two real triangles exhaust the perimeter incidence");
    if (incidence[i] == 2 && count < 2) common[count++] = i;
  }
  check(count == 2 && common[1] - common[0] == 2,
        "Retained source triangle pair covers the quad on one opposite-vertex "
        "diagonal");
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
auto no_new_authority(const BoardingSourceEndpointLoadPayload& l) -> void {
  check(!l.complete && !l.load_qualified && !l.projected_margin_certified &&
            l.certified_resultant_disk_radius_metres == 0,
        "Refused result cannot promote force identities to finite load "
        "qualification");
}
auto quad_controls() -> void {
  const std::array<RigidVector3, 4> square{
      {{1, 0, -1}, {-1, 0, -1}, {-1, 0, 1}, {1, 0, 1}}};
  const auto valid =
      require(detail::boarding_source_endpoint_load_quad_math(square, 0));
  inspect_quad(valid, square, 0);
  check(valid.valid, "Independent clockwise horizontal square is genuine "
                     "upward convex geometry");
  std::array<std::array<RigidVector3, 4>, 6> bad{};
  for (auto& q : bad)
    q = square;
  std::reverse(bad[0].begin(), bad[0].end()); // Downward winding.
  std::swap(bad[1][1], bad[1][2]);            // Bow tie.
  bad[2][2] = {0, 0, -.5};                    // Strict concavity.
  bad[3][1] = bad[3][0];                      // Zero-length edge.
  bad[4][2].y =
      std::nextafter(0., 1.); // Even subnormal tilt is not projected away.
  for (auto& p : bad[5])
    p.z = 0; // Complete collinearity.
  for (const auto& q : bad) {
    const auto e =
        require(detail::boarding_source_endpoint_load_quad_math(q, 0));
    check(
        !e.valid,
        "Reversed/bow-tie/concave/duplicate/tilted/collinear support refuses");
    inspect_quad(e, q, 0);
  }
  auto thin = square;
  for (auto& p : thin)
    p.x = std::copysign(std::numeric_limits<double>::denorm_min(), p.x);
  const auto unresolved =
      require(detail::boarding_source_endpoint_load_quad_math(thin, 0));
  check(!unresolved.valid,
        "Unresolvable subnormal quad signs cannot grant valid support");
  auto shifted = square;
  for (auto& p : shifted)
    p.y = std::nextafter(0., 1.);
  check(!require(detail::boarding_source_endpoint_load_quad_math(shifted, 0))
             .valid,
        "Equal tiny displaced corners still cannot substitute a different "
        "declared plane");
  auto boundary = square;
  for (auto& p : boundary) {
    p.x *= 8;
    p.z *= 8;
    p.y = 8;
  }
  check(require(detail::boarding_source_endpoint_load_quad_math(boundary, 8))
            .valid,
        "Finite closed workspace endpoints are admitted to local quad "
        "arithmetic only");
  for (double v :
       std::array{std::numeric_limits<double>::quiet_NaN(),
                  std::numeric_limits<double>::infinity(),
                  -std::numeric_limits<double>::infinity(),
                  std::nextafter(8., 9.), std::nextafter(-8., -9.)}) {
    for (std::size_t axis = 0; axis < 3; ++axis) {
      auto q = square;
      if (axis == 0)
        q[0].x = v;
      else if (axis == 1)
        q[0].y = v;
      else
        q[0].z = v;
      check(
          !detail::boarding_source_endpoint_load_quad_math(q, 0),
          "Every nonfinite/excessive quad coordinate refuses before geometry");
    }
    check(!detail::boarding_source_endpoint_load_quad_math(square, v),
          "Nonfinite/excessive plane cannot enroll a projected quad");
  }
}
auto pressure_math(const OriginBoardingBootSupport& p, std::size_t site,
                   std::size_t part, double x, double z) -> Math {
  return require(detail::boarding_source_endpoint_load_pressure_math(
      p, site, part, {Bounds{x, x}, Bounds{z, z}}));
}
auto inspect_math(const Math& e, const OriginBoardingBootSupport& p,
                  std::size_t site, std::size_t part, double x, double z)
    -> void {
  const auto source = p.selected_partitions();
  inspect_quad(e.source_quad, source[part].perimeter_metres,
               source[part].plane_metres);
  const Wide center{static_cast<long double>(.16) +
                        (site == 0 ? -static_cast<long double>(.14)
                                   : static_cast<long double>(.14)),
                    0, static_cast<long double>(site == 0 ? -.5 : -.8)};
  inspect_edges(e.pressure.sole_edges, sole(center), {x, z});
  if (e.pressure.coplanar)
    inspect_edges(e.pressure.source_edges, polygon(source[part]), {x, z});
  check(e.edge_checks <= 8,
        "Local pressure checks are bounded by two four-edge sets");
  if (e.pressure.complete)
    check(e.source_quad.valid && e.pressure.arithmetic_supported &&
              e.pressure.coplanar && e.pressure.sole_disk_contained &&
              e.pressure.source_disk_contained,
          "Complete local pressure requires both genuine finite margin proofs");
}
auto pressure_controls(const OriginBoardingBootSupport& p) -> void {
  const auto x = static_cast<double>(static_cast<long double>(.16) -
                                     static_cast<long double>(.14));
  const auto good = pressure_math(p, 0, 8, x, -.46);
  inspect_math(good, p, 0, 8, x, -.46);
  check(good.pressure.complete && good.pressure.sole_disk_contained &&
            good.pressure.source_disk_contained,
        "Registered geometric upper pressure independently lies inside sole "
        "and real tile");
  const auto low_x = static_cast<double>(static_cast<long double>(.16) +
                                         static_cast<long double>(.14));
  const auto low = pressure_math(p, 1, 9, low_x, -.8);
  inspect_math(low, p, 1, 9, low_x, -.8);
  check(
      low.pressure.complete,
      "Registered transition geometric pressure is finite sole/source support");
  const auto wrong_plane = pressure_math(p, 0, 9, x, -.46);
  check(!wrong_plane.pressure.coplanar &&
            !wrong_plane.pressure.source_disk_contained &&
            !wrong_plane.pressure.complete && wrong_plane.edge_checks == 4,
        "Another height's valid quad retains sole-only work and cannot supply "
        "pressure support");
  const auto gap = pressure_math(p, 0, 8, x, -.55);
  inspect_math(gap, p, 0, 8, x, -.55);
  check(gap.pressure.sole_disk_contained &&
            !gap.pressure.source_disk_contained && !gap.pressure.complete,
        "A genuine sole-contained pressure in the inter-source gap still "
        "refuses source support");
  const auto outside = pressure_math(p, 0, 8, 2., -.46);
  inspect_math(outside, p, 0, 8, 2., -.46);
  check(!outside.pressure.sole_disk_contained && !outside.pressure.complete,
        "Large positive squared gap never admits a negative signed-side "
        "pressure");
  const auto required =
      static_cast<long double>(.020) + static_cast<long double>(.010);
  const auto edge_x = static_cast<long double>(.16) -
                      static_cast<long double>(.14) +
                      static_cast<long double>(.06) - required;
  const auto rounded = static_cast<double>(edge_x);
  const auto minus = std::nextafter(rounded,
                                    -std::numeric_limits<double>::infinity()),
             plus = std::nextafter(rounded,
                                   std::numeric_limits<double>::infinity());
  check(static_cast<long double>(minus) < edge_x &&
            static_cast<long double>(plus) > edge_x,
        "Adjacent represented pressures bracket the exact stored-term sole "
        "margin");
  for (double boundary : std::array{minus, rounded, plus}) {
    const auto e = pressure_math(p, 0, 8, boundary, -.46);
    inspect_math(e, p, 0, 8, boundary, -.46);
    if (static_cast<long double>(boundary) > edge_x)
      check(
          !e.pressure.sole_disk_contained,
          "Exact adjacent sole intrusion cannot receive a margin certificate");
  }
  const auto source_edge =
      static_cast<long double>(
          p.selected_partitions()[8].perimeter_metres[0].z) +
      required;
  const auto source_rounded = static_cast<double>(source_edge);
  for (double z :
       std::array{std::nextafter(source_rounded,
                                 -std::numeric_limits<double>::infinity()),
                  source_rounded,
                  std::nextafter(source_rounded,
                                 std::numeric_limits<double>::infinity())}) {
    const auto e = pressure_math(p, 0, 8, x, z);
    inspect_math(e, p, 0, 8, x, z);
    if (static_cast<long double>(z) < source_edge)
      check(!e.pressure.source_disk_contained,
            "Adjacent actual source-edge disk intrusion cannot be rounded into "
            "support");
  }
  const auto broad =
      require(detail::boarding_source_endpoint_load_pressure_math(
          p, 0, 8, {Bounds{x - .2, x + .2}, Bounds{-.46, -.46}}));
  check(!broad.pressure.sole_disk_contained && !broad.pressure.complete,
        "Whole interval pressure domain cannot be replaced by an admissible "
        "midpoint");
  for (std::size_t axis = 0; axis < 2; ++axis) {
    for (Bounds b : std::array{
             Bounds{1, -1}, Bounds{std::numeric_limits<double>::quiet_NaN(), 0},
             Bounds{0, std::numeric_limits<double>::infinity()},
             Bounds{-std::numeric_limits<double>::infinity(), 0}, Bounds{-9, 0},
             Bounds{0, 9}}) {
      std::array<Bounds, 2> input{Bounds{x, x}, Bounds{-.46, -.46}};
      input[axis] = b;
      check(
          !detail::boarding_source_endpoint_load_pressure_math(p, 0, 8, input),
          "Malformed/nonfinite/reversed/excessive pressure bounds refuse "
          "before edge checks");
    }
  }
  check(!detail::boarding_source_endpoint_load_pressure_math(
            p, 2, 8, {Bounds{0, 0}, Bounds{0, 0}}) &&
            !detail::boarding_source_endpoint_load_pressure_math(
                p, 0, 10, {Bounds{0, 0}, Bounds{0, 0}}),
        "Site/source indices cannot escape the fixed local arithmetic roster");
}

auto no_child_work(const Load& d) -> void {
  no_new_authority(d.load);
  check(!d.self.complete && d.load.first_refusal &&
            d.load.first_refusal->condition ==
                BoardingSourceEndpointLoadCondition::self_prerequisite &&
            d.load.checked_quads == 0 && d.load.pressure_candidates == 0 &&
            d.load.edge_checks == 0,
        "Incomplete unchanged child starts no new quad/pressure/edge work");
}
auto capacity_controls(const OriginBoardingBootSupport& p) -> void {
  for (const auto& caps :
       std::array{std::array<std::size_t, 4>{0, 1, 105, 14},
                  std::array<std::size_t, 4>{9, 1, 105, 14},
                  std::array<std::size_t, 4>{10, 0, 105, 14},
                  std::array<std::size_t, 4>{10, 1, 0, 14},
                  std::array<std::size_t, 4>{10, 1, 104, 14},
                  std::array<std::size_t, 4>{10, 1, 105, 0}}) {
    const auto expected = require(detail::boarding_source_endpoint_self_bounded(
        p, caps[0], caps[1], caps[2], caps[3]));
    const auto d = require(detail::boarding_source_endpoint_load_bounded(
        p, caps[0], caps[1], caps[2], caps[3], 10));
    check(snapshot(d.self) == snapshot(expected),
          "Lowered child prefix retains every genuine original "
          "result/counter/refusal bit");
    no_child_work(d);
  }
  for (std::size_t cap : std::array<std::size_t, 4>{0, 1, 8, 9}) {
    const auto d = require(
        detail::boarding_source_endpoint_load_bounded(p, 10, 1, 105, 14, cap));
    const auto& l = d.load;
    no_new_authority(l);
    check(l.first_refusal && l.checked_quads == cap &&
              l.pressure_candidates == 2 * cap,
          "Reduced pressure cap retains declared finite quad and both pressure "
          "prefixes");
    for (const auto& pressure : l.pressures) {
      check(pressure.scanned_partitions == cap && !pressure.coverage_complete &&
                !pressure.complete,
            "Contained prefix never substitutes for full fixed pressure "
            "coverage");
      for (std::size_t i = cap; i < 10; ++i)
        check(pressure.candidates[i].status ==
                  BoardingSourceEndpointLoadCandidateStatus::unexamined,
              "Unvisited pressure candidates retain unknown default status");
    }
    for (std::size_t i = cap; i < 10; ++i)
      check(l.source_quads[i].checked_edges == 0 &&
                l.source_quads[i].checked_side_signs == 0 &&
                !l.source_quads[i].valid,
            "Unvisited source guards do not invent geometric positives");
    if (cap == 0)
      check(l.edge_checks == 0 && l.first_refusal &&
                l.first_refusal->condition ==
                    BoardingSourceEndpointLoadCondition::pressure_capacity,
            "Zero pressure cap performs no hidden sole predicates and retains "
            "capacity refusal");
    else
      check(l.edge_checks <= 8 + 8 * cap,
            "Reduced prefix bounds actual disk-edge work");
    if (cap == 1)
      check(snapshot(d) ==
                snapshot(require(detail::boarding_source_endpoint_load_bounded(
                    p, 10, 1, 105, 14, cap))),
            "Same bounded attempt retains its genuine first refusal and exact "
            "prefix without search");
  }
  check(
      !detail::boarding_source_endpoint_load_bounded(p, 11) &&
          !detail::boarding_source_endpoint_load_bounded(p, 10, 2) &&
          !detail::boarding_source_endpoint_load_bounded(p, 10, 1, 106) &&
          !detail::boarding_source_endpoint_load_bounded(p, 10, 1, 105, 15) &&
          !detail::boarding_source_endpoint_load_bounded(p, 10, 1, 105, 14, 11),
      "Every capacity is reduction-only, without hidden larger-domain "
      "enrollment");
}
struct SavedEnvironment {
  std::fenv_t saved{};
  SavedEnvironment() {
    if (std::fegetenv(&saved) != 0)
      throw std::runtime_error("Cannot retain arithmetic environment");
  }
  SavedEnvironment(const SavedEnvironment&) = delete;
  auto operator=(const SavedEnvironment&) -> SavedEnvironment& = delete;
  ~SavedEnvironment() {
    check(std::fesetenv(&saved) == 0,
          "Restore original arithmetic environment");
  }
};
auto denied_environment(const OriginBoardingBootSupport& p) -> void {
  const auto d = require(detail::boarding_source_endpoint_load_bounded(p));
  no_child_work(d);
  check(!d.load.arithmetic_supported && !d.load.positive_reactions &&
            !d.load.paired_com_x_identity && !d.load.vertical_force_identity &&
            !d.load.vertical_moment_identity,
        "Unsupported environment cannot grant new exact binding/force/moment "
        "positives");
  const std::array<RigidVector3, 4> square{
      {{1, 0, -1}, {-1, 0, -1}, {-1, 0, 1}, {1, 0, 1}}};
  const auto q =
      require(detail::boarding_source_endpoint_load_quad_math(square, 0));
  check(!q.valid && !q.arithmetic_supported,
        "Local quad honors the same RN/subnormal environment domain");
  const auto e = pressure_math(p, 0, 8, .02, -.46);
  check(!e.pressure.complete && !e.pressure.sole_disk_contained &&
            !e.pressure.source_disk_contained,
        "Local pressure cannot silently waive arithmetic environment failure");
}
auto environment_controls(const OriginBoardingBootSupport& p) -> void {
  for (int mode : std::array{FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO}) {
    SavedEnvironment saved;
    check(std::fesetround(mode) == 0,
          "Set independently directed-rounding adversary");
    denied_environment(p);
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
    denied_environment(p);
  }
  for (unsigned mode : std::array{1U, 2U, 3U}) {
    SavedEnvironment saved;
    SavedControl control;
    _mm_setcsr((control.value & ~(3U << 13)) | (mode << 13));
    denied_environment(p);
  }
#endif
  check(std::fegetround() == FE_TONEAREST,
        "Restore nearest mode before first public fixed-load observation");
}
auto inspect(const Load& d, const Oracle& o) -> void {
  const auto& l = d.load;
  check(l.load_version == 1 && l.checked_quads <= 10 &&
            l.pressure_candidates <= 20 && l.edge_checks <= 88,
        "Finite registered new load output and counters remain bounded");
  check(bits(l.disk_radius_metres, .020) &&
            bits(l.disk_edge_margin_metres, .010) &&
            bits(l.required_projected_load_margin_metres, .010),
        "Disk radius, finite contact-edge margin and projected load margin "
        "remain distinct fixed quantities");
  check(!d.world_qualified && !d.route_qualified && !d.continuous_qualified &&
            !d.dynamics_qualified && !d.strength_qualified &&
            !d.actor_qualified && !d.seat_qualified && !d.save_qualified &&
            !d.first_flight_qualified,
        "Static proxy load never admits world, motion, strength or player "
        "actions");
  if (!d.self.complete) {
    no_child_work(d);
    return;
  }
  if (d.self.endpoint.body) {
    const auto& b = *d.self.endpoint.body;
    for (std::size_t i = 0; i < 18; ++i)
      point_contains(b.points[i], o.points[i]);
    for (std::size_t i = 0; i < 15; ++i) {
      const auto& m = d.self.endpoint.parts[i].mass;
      check(static_cast<std::size_t>(m.first) == masses[i].first &&
                static_cast<std::size_t>(m.second) == masses[i].second &&
                m.weight == masses[i].weight,
            "Load owns every original mass endpoint ID/midpoint weight");
      point_contains(b.mass_points[i], o.mass_points[i]);
    }
    auto canonical_com = o.com;
    canonical_com.y -= static_cast<long double>(.847);
    point_contains(b.center_of_mass, canonical_com);
  }
  if (l.paired_com_x_identity)
    check(l.bindings_complete && d.self.endpoint.mass_model_complete,
          "Paired-X authority requires authenticated unchanged full mass "
          "bindings");
  if (l.positive_reactions)
    check(
        bits(l.reaction_fractions[0], .5) &&
            bits(l.reaction_fractions[1], .5) &&
            l.reaction_fractions[0] + l.reaction_fractions[1] == 1,
        "Positive half reactions retain exact fractions and exact complement");
  if (l.arithmetic_supported && l.bindings_complete) {
    contains(l.barycenter_z, o.barycenter.z);
    contains(l.common_pressure_delta_z, o.delta.z);
  }
  const auto parts = d.self.endpoint.sites.source.selected_partitions();
  for (std::size_t i = 0; i < l.checked_quads; ++i) {
    inspect_quad(l.source_quads[i], parts[i].perimeter_metres,
                 parts[i].plane_metres);
    triangle_binding(parts[i]);
  }
  std::size_t counted{}, counted_edges{};
  for (std::size_t site = 0; site < 2; ++site) {
    const auto& p = l.pressures[site];
    counted += p.scanned_partitions;
    // Each actually processed site tests its four sole edges. The finite
    // source prefix tests four disk edges only for valid same-plane quads;
    // perimeter/triangle validity work is excluded from this counter.
    if (p.scanned_partitions > 0) counted_edges += 4;
    for (std::size_t i = 0; i < p.scanned_partitions; ++i)
      if (l.source_quads[i].valid && parts[i].plane_metres == p.plane_metres)
        counted_edges += 4;
    if (p.arithmetic_supported) {
      point_contains(p.pressure_bounds_metres, o.pressures[site]);
      inspect_edges(p.sole_edges, sole(o.centers[site]),
                    {o.pressures[site].x, o.pressures[site].z});
    }
    if (p.source_partition) {
      check(*p.source_partition < 10 &&
                *p.source_partition < p.scanned_partitions,
            "Selected finite support belongs to the actually examined prefix");
      if (*p.source_partition < 10) {
        const auto& source = parts[*p.source_partition];
        check(p.source_keys[0] == source.faces[0].key &&
                  p.source_keys[1] == source.faces[1].key &&
                  bits(p.plane_metres, source.plane_metres) && p.coplanar,
              "Selected support binds both genuine source keys and exact "
              "actual plane");
        inspect_edges(p.source_edges, polygon(source),
                      {o.pressures[site].x, o.pressures[site].z});
      }
    }
    for (std::size_t i = 0; i < 10; ++i) {
      const auto& c = p.candidates[i];
      if (i >= p.scanned_partitions)
        check(c.status == BoardingSourceEndpointLoadCandidateStatus::unexamined,
              "Unvisited public source candidate remains unknown");
      else if (!bits(parts[i].plane_metres, p.plane_metres))
        check(c.status ==
                  BoardingSourceEndpointLoadCandidateStatus::noncoplanar,
              "Visited unequal-height candidate remains noncoplanar rather "
              "than projected contact");
      else if (c.arithmetic_supported && c.quad_valid) {
        const auto corners = polygon(parts[i]);
        auto side_min = std::numeric_limits<long double>::infinity(),
             gap_min = side_min;
        const auto required =
            static_cast<long double>(.020) + static_cast<long double>(.010);
        for (std::size_t edge = 0; edge < 4; ++edge) {
          const auto side =
              signed_side(corners[edge], corners[(edge + 1) % 4],
                          {o.pressures[site].x, o.pressures[site].z});
          side_min = std::min(side_min, side);
          gap_min = std::min(
              gap_min,
              side * side - required * required *
                                edge_length_squared(corners[edge],
                                                    corners[(edge + 1) % 4]));
        }
        contains(c.minimum_signed_side, side_min);
        contains(c.minimum_squared_gap, gap_min);
      }
      if (c.status == BoardingSourceEndpointLoadCandidateStatus::contained)
        check(c.arithmetic_supported && c.quad_valid &&
                  c.minimum_signed_side.supported &&
                  c.minimum_signed_side.lower > 0 &&
                  c.minimum_squared_gap.supported &&
                  c.minimum_squared_gap.lower >= 0,
              "Compact contained status retains actual supported minimum edge "
              "inequalities");
    }
    if (p.complete)
      check(p.arithmetic_supported && p.coplanar && p.sole_disk_contained &&
                p.source_disk_contained && p.coverage_complete &&
                p.scanned_partitions == 10 && p.source_partition,
            "Each complete positive foot owns finite full-prefix sole/source "
            "evidence");
  }
  check(l.pressure_candidates == counted,
        "Global counter counts the two actual visited candidate prefixes");
  check(l.edge_checks == counted_edges,
        "Disk-edge counter equals actual sole and valid coplanar prefix work, "
        "excluding quad guards");
  if (l.complete)
    check(d.self.complete && l.bindings_complete && l.arithmetic_supported &&
              l.paired_com_x_identity && l.positive_reactions &&
              l.projected_barycenter_identity && l.vertical_force_identity &&
              l.vertical_moment_identity && l.placement_nonpenetrating &&
              l.contact_supported && l.pressures[0].complete &&
              l.pressures[1].complete && l.projected_margin_certified &&
              l.load_qualified && !l.first_refusal &&
              bits(l.certified_resultant_disk_radius_metres, .020) &&
              bits(l.required_projected_load_margin_metres, .010),
          "Only full genuine disk evidence and shared algebra can grant the "
          "static normalized load");
  else
    check(!l.load_qualified && !l.projected_margin_certified,
          "Unresolved finite pressure cannot become a load pass from algebra "
          "alone");
}
auto observe(const OriginBoardingBootSupport& p) -> Load {
  auto d = require(assess_origin_boarding_source_endpoint_load(p));
  const auto& l = d.load;
  std::cout << std::setprecision(17) << "fixed load complete=" << l.complete
            << " qualified=" << l.load_qualified
            << " arithmetic=" << l.arithmetic_supported
            << " bindings=" << l.bindings_complete
            << " force=" << l.vertical_force_identity
            << " moment=" << l.vertical_moment_identity
            << " projected=" << l.projected_margin_certified
            << " child=" << d.self.complete << " quads=" << l.checked_quads
            << " candidates=" << l.pressure_candidates
            << " edges=" << l.edge_checks << " barycenterZ=["
            << l.barycenter_z.lower << ',' << l.barycenter_z.upper
            << "] deltaZ=[" << l.common_pressure_delta_z.lower << ','
            << l.common_pressure_delta_z.upper << ']';
  if (l.first_refusal)
    std::cout << " refusal="
              << static_cast<unsigned>(l.first_refusal->condition);
  std::cout << '\n';
  for (std::size_t i = 0; i < 2; ++i) {
    const auto& pressure = l.pressures[i];
    std::cout << "pressure site=" << i << " complete=" << pressure.complete
              << " sole=" << pressure.sole_disk_contained
              << " source=" << pressure.source_disk_contained
              << " plane=" << pressure.plane_metres << " x=["
              << pressure.pressure_bounds_metres[0].x << ','
              << pressure.pressure_bounds_metres[1].x << "] z=["
              << pressure.pressure_bounds_metres[0].z << ','
              << pressure.pressure_bounds_metres[1].z
              << "] prefix=" << pressure.scanned_partitions;
    if (pressure.source_partition)
      std::cout << " selected=" << *pressure.source_partition;
    std::cout << '\n';
  }
  // The registered outcome has not been queried. Only derived implications and
  // bounds are asserted below; Root may freeze an outcome after first logs.
  inspect(d, oracle());
  return d;
}
auto ownership_controls(const Load& original) -> void {
  const auto expected = snapshot(original);
  auto independent = [] {
    auto binding = require(make_native_starting_assembly_binding(
        NativeStartingAssemblySelection{}));
    auto provider = require(make_origin_boarding_boot_support(binding));
    auto result =
        require(assess_origin_boarding_source_endpoint_load(provider));
    auto retained = std::move(provider);
    // NOLINTNEXTLINE(bugprone-use-after-move) -- Empty-handle API contract.
    check(!assess_origin_boarding_source_endpoint_load(provider),
          "Moved provider cannot grant a new load result");
    check(retained.selected_partitions().size() == 10,
          "Moved-to provider retains exact source lifetime");
    return result;
  }();
  check(snapshot(independent) == expected,
        "All load/child source keys and name views survive input handle "
        "destruction");
  auto copy = independent;
  auto moved = std::move(independent);
  check(snapshot(copy) == expected && snapshot(moved) == expected,
        "Copied/moved records preserve every registered field bit");
  copy.load.reaction_fractions = {0, 1};
  copy.load.pressures[0].plane_metres = 8;
  if (copy.self.endpoint.body)
    copy.self.endpoint.body->center_of_mass.lower.z = 8;
  check(snapshot(moved) == expected,
        "Mutable report copy cannot change another owned complete result");
  check(snapshot(require(assess_origin_boarding_source_endpoint_load(
            copy.self.endpoint.sites.source))) == expected,
        "New assessment derives from immutable provider, never copied "
        "success/pressure/COM fields");
}
} // namespace
int main() {
  try {
    static_assert(sizeof(BoardingSourceEndpointLoadPayload) <= 4096);
    static_assert(!Math::load_qualified && !Math::force_qualified &&
                  !Math::body_qualified && !Math::self_qualified);
    if (std::numeric_limits<long double>::digits < 64)
      throw std::runtime_error(
          "Independent unfactored oracle requires64 mantissa bits");
    const auto binding = require(make_native_starting_assembly_binding(
        NativeStartingAssemblySelection{}));
    const auto provider = require(make_origin_boarding_boot_support(binding));
    // Local invalid geometry/pressure, environment and finite-prefix controls
    // precede the first public full fixed-load outcome and its printed
    // evidence.
    quad_controls();
    pressure_controls(provider);
    environment_controls(provider);
    capacity_controls(provider);
    const auto unchanged = snapshot(
        require(assess_origin_boarding_source_endpoint_self(provider)));
    const auto actual = observe(provider);
    // Frozen only after identical first GCC/Clang observations (1540 checks).
    check(
        actual.load.complete && actual.load.load_qualified &&
            actual.load.contact_supported &&
            actual.load.projected_margin_certified &&
            !actual.load.first_refusal,
        "Observed fixed actual-source endpoint retains its physical load pass");
    check(actual.load.pressures[0].source_partition == 8 &&
              actual.load.pressures[1].source_partition == 9,
          "Observed load pressures retain their actual finite upper/tread "
          "sources");
    check(actual.load.checked_quads == 10 &&
              actual.load.pressure_candidates == 20 &&
              actual.load.edge_checks == 48,
          "Observed complete scan retains all source guards and only coplanar "
          "disk work");
    check(snapshot(actual.self) == unchanged,
          "Load owns every unchanged Self01/endpoint/Sites02 result, source "
          "binding and original counter");
    ownership_controls(actual);
    check(snapshot(require(assess_origin_boarding_source_endpoint_self(
              provider))) == unchanged,
          "All new load/control queries preserve the old independent "
          "static-self output bit-for-bit");
  } catch (const std::exception& e) {
    ++failures;
    std::cerr << "EXCEPTION: " << e.what() << '\n';
  }
  std::cout << checks << " fixed endpoint load checks, " << failures
            << " failures\n";
  return failures == 0 ? 0 : 1;
}
