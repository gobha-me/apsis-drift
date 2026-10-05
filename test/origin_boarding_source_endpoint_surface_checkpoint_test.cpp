#include "apsis_drift/origin_boarding_source_endpoint_surface_checkpoint.hpp"
#include "origin_boarding_source_endpoint_load_internal.hpp"
#include "origin_boarding_source_endpoint_self_internal.hpp"
#include "origin_boarding_source_endpoint_surface_checkpoint_internal.hpp"
#include "origin_lower_cockpit_contact_internal.hpp"

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
#include <type_traits>
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
  if (!value)
    throw std::runtime_error("Required owned surface-checkpoint API refused");
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
using Surface = BoardingSourceEndpointSurfaceCheckpointDiagnostic;
using Solid = detail::BoardingSourceEndpointSurfaceCheckpointSolidBounds;
using Shape = detail::BoardingSourceEndpointSurfaceCheckpointShape;
using Pair = detail::BoardingSourceEndpointSurfaceCheckpointPairMathEvidence;
using Condition = BoardingSourceEndpointSurfaceCheckpointCondition;
using Triangle = std::array<RigidVector3, 3>;
auto singleton(RigidVector3 p) -> BoardingPlantedLegPointBounds {
  return {p, p};
}
auto box(RigidVector3 center = {}, RigidVector3 half = {.25, .25, .25})
    -> Solid {
  return {singleton(center), singleton(center), half, 0, Shape::box};
}
auto capsule(RigidVector3 a = {0, -.25, 0}, RigidVector3 b = {0, .25, 0},
             double radius = .125) -> Solid {
  return {singleton(a), singleton(b), {}, radius, Shape::capsule};
}
auto wide(RigidVector3 p) -> Wide {
  return {p.x, p.y, p.z};
}
auto narrow(Wide p) -> RigidVector3 {
  return {static_cast<double>(p.x), static_cast<double>(p.y),
          static_cast<double>(p.z)};
}
auto support(const Solid& s, double y, RigidVector3 n)
    -> BoardingFootSiteScalarBounds {
  return require(
      detail::boarding_source_endpoint_surface_checkpoint_support_math(s, y,
                                                                       n));
}
auto pair(const Solid& s, const Triangle& t, std::size_t axes = 16,
          double y = 0) -> Pair {
  return require(detail::boarding_source_endpoint_surface_checkpoint_pair_math(
      s, y, t, axes));
}
// Enumerate all eight corners, rather than duplicating interval dot/support.
auto corners(const BoardingPlantedLegPointBounds& p) -> std::array<Wide, 8> {
  std::array<Wide, 8> out{};
  for (std::size_t i = 0; i < out.size(); ++i)
    out[i] = {i & 1 ? p.upper.x : p.lower.x, i & 2 ? p.upper.y : p.lower.y,
              i & 4 ? p.upper.z : p.lower.z};
  return out;
}
auto maximizing_point(const Solid& s, double common, RigidVector3 direction)
    -> Wide {
  const auto n = wide(direction);
  const auto select = [&](const BoardingPlantedLegPointBounds& p) {
    const auto options = corners(p);
    return *std::max_element(
        options.begin(), options.end(),
        [&](Wide a, Wide b) { return dot(a, n) < dot(b, n); });
  };
  auto p = select(s.first);
  if (s.shape == Shape::box) {
    std::array<Wide, 8> options{};
    for (std::size_t i = 0; i < options.size(); ++i)
      options[i] =
          add(p, {i & 1 ? s.half_size_metres.x : -s.half_size_metres.x,
                  i & 2 ? s.half_size_metres.y : -s.half_size_metres.y,
                  i & 4 ? s.half_size_metres.z : -s.half_size_metres.z});
    p = *std::max_element(options.begin(), options.end(), [&](Wide a, Wide b) {
      return dot(a, n) < dot(b, n);
    });
  } else {
    const auto q = select(s.second);
    if (dot(q, n) > dot(p, n)) p = q;
    const auto length = std::sqrt(dot(n, n));
    if (length != 0) p = add(p, scale(n, s.radius_metres / length));
  }
  p.y += common;
  return p;
}
auto inspect_pair(const Solid& s, double y, const Triangle& t, const Pair& e,
                  std::size_t cap) -> void {
  check(e.axes_examined <= cap && e.unsupported_axes <= e.axes_examined,
        "Local finite counters preserve every attempted/unsupported axis");
  check(!e.body_qualified && !e.self_qualified && !e.load_qualified &&
            !e.surface_qualified && !e.world_qualified && !e.route_qualified &&
            !e.actor_qualified,
        "Private numeric certificate grants no owned geometry qualification");
  if (!e.certified) return;
  check(e.arithmetic_supported && e.certificate_gap.supported &&
            e.certificate_gap.lower >= 0,
        "Granted separation has a supported nonnegative outward gap");
  const auto n = wide(e.separating_direction);
  // A broad certificate can have no proposal direction. Other certificates
  // are independently checked at the actual stored separating vector.
  if (dot(n, n) == 0) {
    check(e.axes_examined == 0, "Only broad separation needs no proposal axis");
    return;
  }
  const auto maximum = dot(maximizing_point(s, y, e.separating_direction), n);
  const auto opposite = narrow(scale(n, -1));
  const auto minimum = dot(maximizing_point(s, y, opposite), n);
  long double low = dot(wide(t[0]), n), high = low;
  for (auto p : t) {
    low = std::min(low, dot(wide(p), n));
    high = std::max(high, dot(wide(p), n));
  }
  const auto gap = std::max(low - maximum, minimum - high);
  check(gap >= -corroboration_error(gap),
        "Independent maximizing points corroborate the actual separating axis");
}
auto support_controls() -> void {
  std::array<Solid, 4> solids{box(), capsule(), capsule({}, {}, .125), box()};
  solids[3].first = {{-.03, -.04, -.05}, {.07, .08, .09}};
  solids[3].second = solids[3].first;
  const std::array<RigidVector3, 9> directions{{{},
                                                {1, 0, 0},
                                                {-1, 0, 0},
                                                {0, 1, 0},
                                                {0, 0, -1},
                                                {1, 2, -3},
                                                {-2, 3, 4},
                                                {64, 64, 64},
                                                {.5, -.25, .125}}};
  for (const auto& s : solids)
    for (auto n : directions) {
      const auto e = support(s, .847, n);
      contains(e, dot(maximizing_point(s, .847, n), wide(n)));
    }
  const auto sphere = capsule({}, {}, .5);
  contains(support(sphere, 0, {3, 4, 0}), 2.5L);
  contains(support(sphere, 0, {1, 1, 1}), .5L * std::sqrt(3.L));
  const auto zero = support(sphere, 0, {});
  check(zero.supported && zero.lower == 0 && zero.upper == 0,
        "Zero direction has exact zero support but no separating authority");
  const auto tiny =
      support(sphere, 0, {std::numeric_limits<double>::denorm_min(), 0, 0});
  check(!tiny.supported,
        "Subnormal norm outside verified product domain cannot gain support");
  // Full box diagonal support must include corners outside the Self ellipsoid.
  const auto trunk = box({}, {.26, .2695, .18});
  const auto full = support(trunk, 0, {1, 1, 1});
  contains(full, static_cast<long double>(.26) + .2695L + .18L);
  check(full.supported &&
            full.lower > std::sqrt(.26L * .26L + .2695L * .2695L + .18L * .18L),
        "Original trunk full corner is retained beyond its self ellipsoid");
}
auto pair_controls() -> void {
  const Triangle remote{{{2, -.1, -.1}, {2, .1, -.1}, {2, 0, .1}}};
  const Triangle crossing{{{0, -1, -1}, {0, 1, -1}, {0, 0, 1}}};
  const Triangle interior{{{0, 0, 0}, {.01, 0, 0}, {0, .01, 0}}};
  const Triangle touching{{{.25, -.1, -.1}, {.25, .1, -.1}, {.25, 0, .1}}};
  for (const auto& s : std::array{box(), capsule(), capsule({}, {}, .125)}) {
    const auto e = pair(s, remote, 0);
    check(
        e.certified && e.axes_examined == 0,
        "Broad disjointness is a real certificate even with zero axis budget");
    inspect_pair(s, 0, remote, e, 0);
    for (const auto& t : std::array{crossing, interior}) {
      for (std::size_t cap : std::array<std::size_t, 4>{0, 1, 8, 16}) {
        const auto f = pair(s, t, cap);
        check(!f.certified,
              "Triangle through a strict original interior cannot separate");
        inspect_pair(s, 0, t, f, cap);
        if (cap == 0)
          check(f.axes_examined == 0 && f.truncated,
                "Nonbroad zero-axis attempt retains genuine truncation");
      }
    }
  }
  const Triangle diagonal{{{.5, .1, 0}, {.1, .5, 0}, {.5, .5, 0}}};
  const auto oblique = pair(box(), diagonal);
  check(oblique.certified && oblique.axes_examined > 0,
        "Nonbroad oblique separation requires a genuine full-box support "
        "certificate");
  inspect_pair(box(), 0, diagonal, oblique, 16);
  const auto boundary = pair(box(), touching);
  check(boundary.certified,
        "Open box boundary contact admits exact surface separation");
  inspect_pair(box(), 0, touching, boundary, 16);
  // A closed shell surrounds the box, but every shell triangle is separated.
  // This is an explicit counterexample to promoting surface to volume
  // clearance.
  const std::array<RigidVector3, 8> cube{{{-2, -2, -2},
                                          {2, -2, -2},
                                          {-2, 2, -2},
                                          {2, 2, -2},
                                          {-2, -2, 2},
                                          {2, -2, 2},
                                          {-2, 2, 2},
                                          {2, 2, 2}}};
  const std::array<std::array<std::size_t, 3>, 12> faces{{{0, 1, 3},
                                                          {0, 3, 2},
                                                          {4, 6, 7},
                                                          {4, 7, 5},
                                                          {0, 4, 5},
                                                          {0, 5, 1},
                                                          {2, 3, 7},
                                                          {2, 7, 6},
                                                          {0, 2, 6},
                                                          {0, 6, 4},
                                                          {1, 5, 7},
                                                          {1, 7, 3}}};
  for (auto indices : faces) {
    const Triangle t{cube[indices[0]], cube[indices[1]], cube[indices[2]]};
    const auto e = pair(box(), t);
    check(e.certified && !e.world_qualified,
          "Separated enclosing shell boundaries do not imply volume clearance");
    inspect_pair(box(), 0, t, e, 16);
  }
}
auto invalid_controls() -> void {
  const auto s = box();
  const Triangle remote{{{2, 0, 0}, {2, .1, 0}, {2, 0, .1}}};
  const std::array bad{std::numeric_limits<double>::quiet_NaN(),
                       std::numeric_limits<double>::infinity(),
                       -std::numeric_limits<double>::infinity(),
                       std::nextafter(8., 9.), std::nextafter(-8., -9.)};
  for (double x : bad) {
    for (std::size_t field = 0; field < 5; ++field) {
      auto b = s;
      if (field == 0) b.first.lower.x = x;
      if (field == 1) b.first.upper.y = x;
      if (field == 2) b.second.lower.z = x;
      if (field == 3) b.half_size_metres.x = x;
      if (field == 4) b.radius_metres = x;
      check(!detail::boarding_source_endpoint_surface_checkpoint_pair_math(
                b, 0, remote),
            "Invalid solid domain refuses before a remote broad shortcut");
    }
    auto t = remote;
    t[2].z = x;
    check(!detail::boarding_source_endpoint_surface_checkpoint_pair_math(s, 0,
                                                                         t) &&
              !detail::boarding_source_endpoint_surface_checkpoint_pair_math(
                  s, x, remote),
          "Invalid all-three triangle/common placement domains refuse");
  }
  auto reversed = s;
  reversed.first.lower.x = 1;
  auto invalid_shape = s;
  invalid_shape.shape = static_cast<Shape>(255);
  auto bad_capsule = capsule();
  bad_capsule.half_size_metres.x = .1;
  auto bad_box = s;
  bad_box.radius_metres = .1;
  check(
      !detail::boarding_source_endpoint_surface_checkpoint_pair_math(
          reversed, 0, remote) &&
          !detail::boarding_source_endpoint_surface_checkpoint_pair_math(
              invalid_shape, 0, remote) &&
          !detail::boarding_source_endpoint_surface_checkpoint_pair_math(
              bad_capsule, 0, remote) &&
          !detail::boarding_source_endpoint_surface_checkpoint_pair_math(
              bad_box, 0, remote) &&
          !detail::boarding_source_endpoint_surface_checkpoint_pair_math(
              s, 0, remote, 17),
      "Reversed bounds, foreign shapes, mixed shapes and expanded cap refuse");
  for (double x : std::array{0., -.1}) {
    auto b = s;
    b.half_size_metres.y = x;
    auto c = capsule();
    c.radius_metres = x;
    check(!detail::boarding_source_endpoint_surface_checkpoint_pair_math(
              b, 0, remote) &&
              !detail::boarding_source_endpoint_surface_checkpoint_pair_math(
                  c, 0, remote),
          "Nonpositive box dimensions/capsule radius cannot grant separation");
  }
  for (double x : std::array{std::nextafter(64., 65.),
                             std::numeric_limits<double>::quiet_NaN(),
                             std::numeric_limits<double>::infinity()})
    check(!detail::boarding_source_endpoint_surface_checkpoint_support_math(
              s, 0, {x, 0, 0}),
          "Private support direction is finite and coefficient-bounded");
  // Finite workspace boundary remains a valid domain, without inventing a gap.
  check(detail::boarding_source_endpoint_surface_checkpoint_pair_math(
            box({8, 8, 8}), 8, remote)
            .has_value(),
        "Closed workspace endpoint is allowed as bounded arithmetic input");
}
auto sole_controls() -> void {
  for (double plane : std::array{kCabinCorridorFloorMetres, -160000.0 * 1e-6}) {
    const std::array terms{plane, .05, -.847};
    const Triangle boundary{{{0, plane, 0}, {.1, plane, 0}, {0, plane, .1}}};
    const auto e =
        require(detail::boarding_source_endpoint_surface_checkpoint_sole_math(
            terms, .847, .05, boundary));
    check(e.arithmetic_supported && e.template_matches &&
              e.all_vertices_at_or_below && bits(e.plane_metres, plane) &&
              !e.surface_qualified && !e.world_qualified,
          "Exact sole cancellation retains stored source plane without "
          "provider authority");
    for (std::size_t i = 0; i < 3; ++i) {
      auto above = boundary;
      above[i].y = std::nextafter(plane, 1.);
      auto below = boundary;
      below[i].y = std::nextafter(plane, -1.);
      check(!require(
                 detail::boarding_source_endpoint_surface_checkpoint_sole_math(
                     terms, .847, .05, above))
                 .all_vertices_at_or_below,
            "Any one nextafter-above vertex defeats sole-only separation");
      check(
          require(detail::boarding_source_endpoint_surface_checkpoint_sole_math(
                      terms, .847, .05, below))
              .all_vertices_at_or_below,
          "All-three exact at/below relation retains boundary certificate");
    }
    auto changed = terms;
    changed[1] = std::nextafter(.05, 1.);
    check(
        !require(detail::boarding_source_endpoint_surface_checkpoint_sole_math(
                     changed, .847, .05, boundary))
                .template_matches &&
            !require(
                 detail::boarding_source_endpoint_surface_checkpoint_sole_math(
                     terms, std::nextafter(.847, 1.), .05, boundary))
                 .template_matches &&
            !require(
                 detail::boarding_source_endpoint_surface_checkpoint_sole_math(
                     terms, .847, std::nextafter(.05, 1.), boundary))
                 .template_matches,
        "Adjacent template/common/half changes cannot inherit exact sole "
        "identity");
    auto compensated = terms;
    const auto altered_common = std::nextafter(.847, 1.);
    compensated[2] = -altered_common;
    check(
        !require(detail::boarding_source_endpoint_surface_checkpoint_sole_math(
                     compensated, altered_common, .05, boundary))
             .template_matches,
        "Simultaneously altered cancellation terms retain no fixed-template "
        "identity");
    auto nonfinite = boundary;
    nonfinite[2].y = std::numeric_limits<double>::quiet_NaN();
    check(!detail::boarding_source_endpoint_surface_checkpoint_sole_math(
              terms, .847, .05, nonfinite),
          "Sole validates every vertex before exact comparisons");
  }
}
struct SavedEnvironment {
  std::fenv_t saved{};
  SavedEnvironment() {
    if (std::fegetenv(&saved) != 0)
      throw std::runtime_error("Cannot preserve arithmetic environment");
  }
  SavedEnvironment(const SavedEnvironment&) = delete;
  auto operator=(const SavedEnvironment&) -> SavedEnvironment& = delete;
  ~SavedEnvironment() {
    check(std::fesetenv(&saved) == 0, "Restore arithmetic environment exactly");
  }
};
auto denied_environment(const OriginBoardingBootSupport& p) -> void {
  const Triangle t{{{2, 0, 0}, {2, .1, 0}, {2, 0, .1}}};
  const auto e = pair(box(), t);
  check(
      !e.arithmetic_supported && !e.certified && e.axes_examined == 0,
      "Environment refusal precedes even a trivially separated broad shortcut");
  check(!support(capsule(), 0, {1, 0, 0}).supported,
        "Norm/support authority refuses the same unsupported environment");
  const auto f =
      require(detail::boarding_source_endpoint_surface_checkpoint_sole_math(
          {kCabinCorridorFloorMetres, .05, -.847}, .847, .05, t));
  check(!f.arithmetic_supported && !f.all_vertices_at_or_below,
        "Sole exact-value shortcut cannot waive arithmetic environment guard");
  const auto d =
      require(detail::boarding_source_endpoint_surface_checkpoint_bounded(
          p, 10, 1, 105, 14, 10, 0));
  check(
      !d.surface.complete && !d.surface.surface_qualified &&
          d.surface.examined_pairs == 0 && d.surface.axes_examined == 0 &&
          d.surface.first_refusal && !d.load.load.complete,
      "Unsupported child environment retains refusal before new surface math");
}
auto environment_controls(const OriginBoardingBootSupport& p) -> void {
  for (int mode : std::array{FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO}) {
    SavedEnvironment saved;
    check(std::fesetround(mode) == 0, "Install directed rounding adversary");
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
        "Restore nearest mode before fixed public observation");
}
auto no_authority(const Surface& d) -> void {
  check(!d.world_qualified && !d.volume_qualified && !d.material_qualified &&
            !d.strength_qualified && !d.dynamics_qualified &&
            !d.sweep_qualified && !d.continuous_qualified &&
            !d.acquisition_qualified && !d.free_foot_swing_qualified &&
            !d.transfer_qualified && !d.route_qualified && !d.actor_qualified &&
            !d.seat_qualified && !d.save_qualified && !d.first_flight_qualified,
        "Surface checkpoint cannot promote closed volume, movement or actor "
        "admission");
}
auto snapshot(const Surface& d) -> Snapshot {
  auto s = snapshot(d.load);
  const auto& p = d.surface;
  s.integer(p.surface_version);
  for (const auto& c : p.coverage) {
    s.point(c.lower_metres);
    s.point(c.upper_metres);
    s.integer(c.part);
    s.integer(c.examined);
    s.integer(c.arithmetic_supported);
    s.integer(c.covered);
  }
  for (auto n : std::array{
           p.effective_triangle_count, p.expected_pairs, p.examined_pairs,
           p.certified_pairs, p.broad_certified_pairs, p.sole_certified_pairs,
           p.support_certified_pairs, p.axes_examined, p.unsupported_axes,
           p.visited_triangles, p.metadata_visited_triangles})
    s.integer(n);
  for (bool b :
       std::array{p.arithmetic_supported, p.coverage_complete,
                  p.comparisons_complete, p.surface_qualified, p.complete})
    s.integer(b);
  s.integer(p.first_refusal.has_value());
  if (p.first_refusal) {
    const auto& r = *p.first_refusal;
    s.integer(r.key.has_value());
    if (r.key) {
      s.integer(r.key->buffer);
      s.integer(r.key->group);
      s.integer(r.key->triangle);
    }
    s.names.emplace_back(r.source_object);
    s.integer(r.pair_index.has_value());
    if (r.pair_index) s.integer(*r.pair_index);
    s.integer(r.axes_examined);
    s.integer(r.unsupported_axes);
    s.integer(r.object);
    s.integer(r.evaluated_source_triangle.has_value());
    if (r.evaluated_source_triangle) s.integer(*r.evaluated_source_triangle);
    s.integer(r.part);
    s.integer(r.condition);
  }
  return s;
}
auto child_capacity_controls(const OriginBoardingBootSupport& p) -> void {
  for (const auto& cap :
       std::array{std::array<std::size_t, 5>{0, 1, 105, 14, 10},
                  std::array<std::size_t, 5>{10, 0, 105, 14, 10},
                  std::array<std::size_t, 5>{10, 1, 0, 14, 10},
                  std::array<std::size_t, 5>{10, 1, 105, 0, 10},
                  std::array<std::size_t, 5>{10, 1, 105, 14, 0}}) {
    const auto old = require(detail::boarding_source_endpoint_load_bounded(
        p, cap[0], cap[1], cap[2], cap[3], cap[4]));
    const auto d =
        require(detail::boarding_source_endpoint_surface_checkpoint_bounded(
            p, cap[0], cap[1], cap[2], cap[3], cap[4], 0));
    check(snapshot(old) == snapshot(d.load),
          "Lowered child preserves complete old fields/refusal/prefix bits");
    check(!d.surface.complete && !d.surface.surface_qualified &&
              d.surface.examined_pairs == 0 && d.surface.certified_pairs == 0 &&
              d.surface.axes_examined == 0 &&
              d.surface.visited_triangles == 0 && d.surface.first_refusal &&
              d.surface.first_refusal->condition ==
                  Condition::load_prerequisite,
          "Incomplete load prerequisite starts no new mathematical work");
    for (const auto& c : d.surface.coverage)
      check(!c.examined && !c.covered,
            "Incomplete child does not invent crop coverage");
    no_authority(d);
  }
  check(
      !detail::boarding_source_endpoint_surface_checkpoint_bounded(p, 11) &&
          !detail::boarding_source_endpoint_surface_checkpoint_bounded(p, 10,
                                                                       2) &&
          !detail::boarding_source_endpoint_surface_checkpoint_bounded(p, 10, 1,
                                                                       106) &&
          !detail::boarding_source_endpoint_surface_checkpoint_bounded(
              p, 10, 1, 105, 15) &&
          !detail::boarding_source_endpoint_surface_checkpoint_bounded(
              p, 10, 1, 105, 14, 11) &&
          !detail::boarding_source_endpoint_surface_checkpoint_bounded(
              p, 10, 1, 105, 14, 10,
              kBoardingSourceEndpointSurfaceMaximumPairs + 1) &&
          !detail::boarding_source_endpoint_surface_checkpoint_bounded(
              p, 10, 1, 105, 14, 10, 0, 17),
      "Every private cap is reduction-only and cannot expand registered work");
}
struct Inventory {
  std::uint64_t total{};
  std::array<std::uint64_t, 3> counts{};
};
auto inventory(const OriginLowerCockpitContact& contact) -> Inventory {
  const auto* original = contact.original_geometry();
  check(original && original->support_catalog() && contact.stowed_partition(),
        "Effective source retains immutable original catalog and whole-object "
        "mask");
  const auto& catalog = *original->support_catalog();
  const auto removed = contact.stowed_partition()->removed();
  std::uint64_t craft{}, removed_count{};
  for (const auto& g : catalog.groups())
    if (g.owner == BoardingOwner::craft) craft += g.triangle_count;
  for (const auto& r : removed)
    removed_count += r.triangles.count;
  check(craft == 399187 && removed_count == 456 && removed.size() == 8,
        "Independent catalog/whole-mask denominator conserves original faces");
  Inventory out;
  bool valid = true;
  std::optional<LowerCockpitTriangleKey> previous;
  const auto result = require(detail::visit_effective_lower_cockpit_contact(
      contact, [&](const auto& t) {
        const auto ns = static_cast<std::size_t>(t.key.buffer);
        if (ns >= out.counts.size()) {
          valid = false;
          return false;
        }
        ++out.counts[ns];
        ++out.total;
        valid = valid && t.obstacle && !t.source_object.empty();
        if (previous) {
          const auto old = static_cast<std::size_t>(previous->buffer);
          valid =
              valid && (old < ns ||
                        (old == ns && (previous->group < t.key.group ||
                                       (previous->group == t.key.group &&
                                        previous->triangle < t.key.triangle))));
        }
        previous = t.key;
        if (t.key.buffer == LowerCockpitContactBuffer::original) {
          if (t.object >= catalog.objects().size() ||
              t.key.group >= catalog.groups().size())
            valid = false;
          else {
            const auto& o = catalog.objects()[t.object];
            valid =
                valid &&
                catalog.groups()[t.key.group].owner == BoardingOwner::craft &&
                o.group == t.key.group && t.source_object == o.source_object &&
                t.key.triangle >= o.triangle_start &&
                t.key.triangle - o.triangle_start < o.triangle_count &&
                !t.evaluated_source_triangle;
            for (const auto& r : removed)
              if (r.group == t.key.group &&
                  t.key.triangle >= r.triangles.start &&
                  t.key.triangle - r.triangles.start < r.triangles.count)
                valid = false;
          }
        } else {
          const auto objects = t.key.buffer == LowerCockpitContactBuffer::halo
                                   ? contact.objects()
                                   : contact.replacement_objects();
          if (t.object >= objects.size())
            valid = false;
          else {
            const auto& o = objects[t.object];
            const auto local = t.key.triangle - o.triangle_start;
            valid = valid && t.key.group == 0 &&
                    t.source_object == o.source_object &&
                    t.key.triangle >= o.triangle_start &&
                    local < o.triangle_count &&
                    local < o.evaluated_source_triangles.size() &&
                    t.evaluated_source_triangle;
            if (local < o.evaluated_source_triangles.size())
              valid = valid && t.evaluated_source_triangle ==
                                   o.evaluated_source_triangles[local];
          }
        }
        return true;
      }));
  check(valid && result.complete && result.visited_triangles == out.total &&
            result.total_triangles == out.total,
        "Complete immutable visitor preserves namespaces, monotone keys, names "
        "and evaluated face identity");
  check(out.counts == std::array<std::uint64_t, 3>{craft - removed_count, 8100,
                                                   1508} &&
            out.total == 408339,
        "Whole-mask conservation plus halo/replacement gives exact effective "
        "denominator");
  const auto stopped = require(detail::visit_effective_lower_cockpit_contact(
      contact, [](const auto&) { return false; }));
  check(
      !stopped.complete && stopped.visited_triangles == 1 &&
          stopped.total_triangles == out.total,
      "Metadata stop callback counts one visit without a mathematical attempt");
  check(!detail::visit_effective_lower_cockpit_contact(contact, {}),
        "Empty visitor callback refuses");
  return out;
}
auto crop_controls(const OriginLowerCockpitContact& c) -> void {
  const auto covered = [&](detail::CabinContactBox b) {
    return require(detail::covers_lower_cockpit_bounds(c, b));
  };
  check(covered({{-1.05, -.2, -3.5}, {1.05, 2.97, 6.35}}) &&
            covered({{-1.05, -.85, -3.5}, {1.05, -.2, -.5}}),
        "Both complete declared closed crop boxes retain exact boundaries");
  check(covered({{0, -.3, -.6}, {.1, 0, -.5}}),
        "Union join can cover a box crossing its closed Y boundary");
  check(!covered({{0, -.3, -.49}, {.1, -.21, -.48}}) &&
            !covered({{0, std::nextafter(-.85, -1.), -1}, {.1, -.8, -.8}}) &&
            !covered({{std::nextafter(-1.05, -2.), 0, 0}, {0, .1, .1}}),
        "Lower-front notch and adjacent outside bounds never inherit coverage");
  check(
      !detail::covers_lower_cockpit_bounds(c, {{1, 0, 0}, {0, 1, 1}}) &&
          !detail::covers_lower_cockpit_bounds(
              c, {{0, 0, 0}, {1, std::numeric_limits<double>::infinity(), 1}}),
      "Crop refuses reversed and nonfinite geometry");
}
constexpr std::array<RigidVector3, 15> original_halves{{{.24, .12, .18},
                                                        {.26, .2695, .18},
                                                        {.16, .18, .18},
                                                        {},
                                                        {},
                                                        {.06, .05, .14},
                                                        {},
                                                        {},
                                                        {.04, .05, .02},
                                                        {},
                                                        {},
                                                        {.06, .05, .14},
                                                        {},
                                                        {},
                                                        {.04, .05, .02}}};
constexpr std::array<double, 15> original_radii{
    0, 0, 0, .105, .075, 0, .065, .055, 0, .105, .075, 0, .065, .055, 0};
auto owned_solid(const BoardingSourceEndpointDiagnostic& d, std::size_t i)
    -> Solid {
  const auto& m = masses[i];
  Solid s{d.body->points[m.first], d.body->points[m.second], original_halves[i],
          original_radii[i],
          original_radii[i] == 0 ? Shape::box : Shape::capsule};
  return s;
}
auto original_shape_controls(const Load& d, const Oracle& o) -> void {
  const auto& endpoint = d.self.endpoint;
  check(endpoint.body.has_value(),
        "Unchanged complete child retains its original eighteen points");
  if (!endpoint.body) return;
  for (std::size_t i = 0; i < o.pressures.size(); ++i)
    point_contains(d.load.pressures[i].pressure_bounds_metres, o.pressures[i]);
  auto canonical_com = o.com;
  canonical_com.y -= static_cast<long double>(.847);
  point_contains(endpoint.body->center_of_mass, canonical_com);
  for (std::size_t i = 0; i < o.points.size(); ++i)
    point_contains(endpoint.body->points[i], o.points[i]);
  std::size_t boxes{}, capsules{};
  for (std::size_t i = 0; i < masses.size(); ++i) {
    const auto& p = endpoint.parts[i];
    const auto& m = masses[i];
    check(static_cast<std::size_t>(p.id) == i &&
              static_cast<std::size_t>(p.mass.first) == m.first &&
              static_cast<std::size_t>(p.mass.second) == m.second &&
              p.mass.weight == m.weight,
          "Original point IDs, ordering and all integer mass bindings are "
          "retained");
    if (original_radii[i] == 0) {
      ++boxes;
      const auto* b =
          std::get_if<BoardingPlantedBodyBoxBinding>(&p.reservation);
      check(b && static_cast<std::size_t>(b->center) == m.first &&
                b->half_size_metres == original_halves[i] &&
                b->frame.columns == BoardingBodyFrame{}.columns,
            "All seven world boxes preserve original full dimensions and "
            "identity axes");
    } else {
      ++capsules;
      const auto* c =
          std::get_if<BoardingPlantedBodyCapsuleBinding>(&p.reservation);
      check(c && static_cast<std::size_t>(c->start) == m.first &&
                static_cast<std::size_t>(c->end) == m.second &&
                bits(c->radius_metres, original_radii[i]),
            "All eight capsules preserve original endpoints/radii");
    }
    const auto s = owned_solid(endpoint, i);
    for (auto n : std::array<RigidVector3, 4>{
             {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {1, -2, 3}}}) {
      const auto e = support(s, endpoint.common_translation_y_metres, n);
      contains(e,
               dot(maximizing_point(s, endpoint.common_translation_y_metres, n),
                   wide(n)));
      // Independent exact-expression point oracle lies inside the owned packet.
      auto reference = s;
      reference.first = singleton(narrow(o.points[m.first]));
      reference.second = singleton(narrow(o.points[m.second]));
      const auto x = dot(maximizing_point(reference, .847, n), wide(n));
      contains(e, x);
    }
  }
  check(boxes == 7 && capsules == 8,
        "Surface geometry is exactly seven original boxes/eight capsules");
}
auto source_at(const OriginLowerCockpitContact& contact, std::uint64_t index)
    -> detail::LowerCockpitEffectiveTriangle {
  std::optional<detail::LowerCockpitEffectiveTriangle> selected;
  std::uint64_t position{};
  const auto visit = require(detail::visit_effective_lower_cockpit_contact(
      contact, [&](const auto& t) {
        if (position++ == index) {
          selected = t;
          return false;
        }
        return true;
      }));
  check(selected && !visit.complete && visit.visited_triangles == index + 1,
        "Stopping identity is reconstructed from the actual immutable "
        "source-order prefix");
  if (!selected) throw std::runtime_error("Missing stopping source identity");
  return *selected;
}
auto inspect_surface(const Surface& d, const Inventory& inv, const Oracle& o,
                     std::uint64_t cap, std::size_t axis_cap) -> void {
  no_authority(d);
  const auto& s = d.surface;
  check(s.surface_version == 1 && s.effective_triangle_count == inv.total &&
            s.expected_pairs == 15 * inv.total &&
            s.metadata_visited_triangles == 1,
        "Observed denominator is actual effective visitor count, separate from "
        "metadata callback");
  check(s.examined_pairs <= cap && s.certified_pairs <= s.examined_pairs &&
            s.broad_certified_pairs + s.sole_certified_pairs +
                    s.support_certified_pairs ==
                s.certified_pairs &&
            s.unsupported_axes <= s.axes_examined &&
            s.axes_examined <= axis_cap * s.examined_pairs,
        "Pair/category/axis counters conserve actual finite attempted work");
  const auto& endpoint = d.load.self.endpoint;
  check(endpoint.body && endpoint.sites.source.contact(),
        "Owned child retains body and immutable source lifetime");
  if (!endpoint.body || !endpoint.sites.source.contact()) return;
  bool unseen = false;
  for (std::size_t i = 0; i < s.coverage.size(); ++i) {
    const auto& c = s.coverage[i];
    check(static_cast<std::size_t>(c.part) == i,
          "Crop record retains original part order");
    if (!c.examined) {
      unseen = true;
      check(!c.covered && !c.arithmetic_supported,
            "Unexamined coverage stays unknown");
      continue;
    }
    check(!unseen, "Coverage work is one uninterrupted original-part prefix");
    if (!c.arithmetic_supported) continue;
    auto reference = owned_solid(endpoint, i);
    reference.first = singleton(narrow(o.points[masses[i].first]));
    reference.second = singleton(narrow(o.points[masses[i].second]));
    for (std::size_t axis = 0; axis < 3; ++axis) {
      RigidVector3 n{};
      if (axis == 0)
        n.x = 1;
      else if (axis == 1)
        n.y = 1;
      else
        n.z = 1;
      const auto high = component(maximizing_point(reference, .847, n), axis);
      const auto low = component(
          maximizing_point(reference, .847, narrow(scale(wide(n), -1))), axis);
      check(component(c.lower_metres, axis) <= low + corroboration_error(low) &&
                high - corroboration_error(high) <=
                    component(c.upper_metres, axis),
            "Crop encloses full independent original shape with common "
            "placement once");
    }
    check(c.covered == require(detail::covers_lower_cockpit_bounds(
                           *endpoint.sites.source.contact(),
                           {c.lower_metres, c.upper_metres})),
          "Crop flag binds actual closed declared source union");
  }
  if (s.examined_pairs != 0)
    check(s.coverage_complete && d.load.load.complete &&
              d.load.load.load_qualified,
          "Every attempted pair requires complete old load and all fifteen "
          "crops");
  if (!s.coverage_complete)
    check(s.examined_pairs == 0 && s.axes_examined == 0 &&
              s.visited_triangles == 0,
          "Unknown/uncovered crop cannot start triangle math");
  if (s.first_refusal) {
    const auto& r = *s.first_refusal;
    check(
        !s.complete && !s.surface_qualified && !s.comparisons_complete,
        "First refusal never promotes a prefix to complete surface clearance");
    if (r.pair_index) {
      const bool capacity = r.condition == Condition::pair_capacity;
      check(*r.pair_index < s.expected_pairs &&
                *r.pair_index ==
                    (capacity ? s.examined_pairs : s.examined_pairs - 1) &&
                static_cast<std::uint64_t>(r.part) ==
                    *r.pair_index / inv.total &&
                s.visited_triangles == s.examined_pairs + (capacity ? 1 : 0),
            "Refusal index distinguishes next-unattempted capacity from "
            "attempted stopping pair");
      if (*r.pair_index < s.expected_pairs) {
        const auto t = source_at(*endpoint.sites.source.contact(),
                                 *r.pair_index % inv.total);
        check(r.key == t.key && r.object == t.object &&
                  r.source_object == t.source_object &&
                  r.evaluated_source_triangle == t.evaluated_source_triangle,
              "First refusal retains exact "
              "namespace/key/object/evaluated-face/name identity");
      }
      if (capacity)
        check(s.examined_pairs == cap &&
                  s.certified_pairs == s.examined_pairs && r.axes_examined == 0,
              "Capacity exhausts only a fully certified prefix before the next "
              "attempt");
      else
        check(s.certified_pairs + 1 == s.examined_pairs &&
                  r.axes_examined <= axis_cap &&
                  r.unsupported_axes <= r.axes_examined,
              "Attempted unresolved pair contributes exactly one uncertified "
              "attempt");
    } else
      check(s.examined_pairs == 0,
            "Pre-pair refusal has no invented pair index");
  }
  if (s.complete)
    check(s.surface_qualified && s.coverage_complete &&
              s.arithmetic_supported && s.comparisons_complete &&
              !s.first_refusal && s.examined_pairs == s.expected_pairs &&
              s.certified_pairs == s.expected_pairs &&
              s.visited_triangles == s.expected_pairs,
          "Completion requires every pair/coverage and supported work, not "
          "absence of a witness");
  check(s.complete == s.surface_qualified,
        "Only the registered surface theorem may complete");
}
auto observe(const OriginBoardingBootSupport& p) -> Surface {
  auto d =
      require(assess_origin_boarding_source_endpoint_surface_checkpoint(p));
  const auto& s = d.surface;
  // The first fixed outcome is recorded BEFORE any outcome-specific regression.
  std::cout << std::setprecision(17)
            << "FIRST source endpoint surface checkpoint"
            << " version=" << s.surface_version
            << " child_load=" << d.load.load.complete
            << " arithmetic=" << s.arithmetic_supported
            << " coverage=" << s.coverage_complete
            << " comparisons=" << s.comparisons_complete
            << " surface=" << s.surface_qualified << " complete=" << s.complete
            << " triangles=" << s.effective_triangle_count
            << " expected=" << s.expected_pairs
            << " examined=" << s.examined_pairs
            << " certified=" << s.certified_pairs
            << " broad=" << s.broad_certified_pairs
            << " sole=" << s.sole_certified_pairs
            << " support=" << s.support_certified_pairs
            << " axes=" << s.axes_examined
            << " unsupported_axes=" << s.unsupported_axes
            << " visits=" << s.visited_triangles
            << " metadata_visits=" << s.metadata_visited_triangles
            << " world=" << d.world_qualified
            << " volume=" << d.volume_qualified << '\n';
  for (const auto& c : s.coverage)
    std::cout << "CROP part=" << static_cast<unsigned>(c.part)
              << " examined=" << c.examined
              << " supported=" << c.arithmetic_supported
              << " covered=" << c.covered << " low=" << c.lower_metres.x << ','
              << c.lower_metres.y << ',' << c.lower_metres.z
              << " high=" << c.upper_metres.x << ',' << c.upper_metres.y << ','
              << c.upper_metres.z << '\n';
  if (s.first_refusal) {
    const auto& r = *s.first_refusal;
    std::cout << "REFUSAL condition=" << static_cast<unsigned>(r.condition)
              << " part=" << static_cast<unsigned>(r.part) << " pair_index=";
    if (r.pair_index)
      std::cout << *r.pair_index;
    else
      std::cout << "none";
    std::cout << " axes=" << r.axes_examined
              << " unsupported=" << r.unsupported_axes << " object=" << r.object
              << " source=" << r.source_object << " evaluated_face=";
    if (r.evaluated_source_triangle)
      std::cout << *r.evaluated_source_triangle;
    else
      std::cout << "none";
    if (r.key)
      std::cout << " key=" << static_cast<unsigned>(r.key->buffer) << ','
                << r.key->group << ',' << r.key->triangle;
    std::cout << '\n';
  } else
    std::cout << "REFUSAL none\n";
  return d;
}
auto prefix_controls(const OriginBoardingBootSupport& p, const Inventory& inv,
                     const Oracle& o, const Snapshot& child) -> void {
  for (std::uint64_t cap : std::array<std::uint64_t, 4>{0, 1, 16, 256}) {
    const auto d =
        require(detail::boarding_source_endpoint_surface_checkpoint_bounded(
            p, 10, 1, 105, 14, 10, cap));
    check(snapshot(d.load) == child,
          "Reduced new pair budget leaves every child field unchanged");
    inspect_surface(d, inv, o, cap, 16);
    if (cap == 0)
      check(d.surface.examined_pairs == 0 && d.surface.certified_pairs == 0,
            "Zero pair cap executes no hidden pair predicate");
  }
  const auto d =
      require(detail::boarding_source_endpoint_surface_checkpoint_bounded(
          p, 10, 1, 105, 14, 10, 16, 0));
  inspect_surface(d, inv, o, 16, 0);
  check(d.surface.axes_examined == 0 &&
            d.surface.support_certified_pairs == 0 &&
            d.surface.sole_certified_pairs == 0,
        "Zero axis cap preserves broad-only work, no hidden sole/support "
        "proposal");
}
auto lifetime_controls(const NativeCraftBinding& binding) -> void {
  auto d = [&] {
    auto original = require(make_origin_boarding_boot_support(binding));
    auto retained = std::move(original);
    // NOLINTBEGIN(bugprone-use-after-move) -- Empty-handle API contract.
    check(!detail::boarding_source_endpoint_surface_checkpoint_bounded(
              original, 10, 1, 105, 14, 10, 0),
          "Moved-from provider refuses a new zero-budget surface assessment");
    // NOLINTEND(bugprone-use-after-move) -- Empty-handle API contract.
    auto contact = *retained.contact();
    auto kept = std::move(contact);
    // NOLINTBEGIN(bugprone-use-after-move) -- Empty-handle API contract.
    check(!detail::visit_effective_lower_cockpit_contact(
              contact, [](const auto&) { return false; }),
          "Moved-from contact refuses even metadata traversal");
    // NOLINTEND(bugprone-use-after-move) -- Empty-handle API contract.
    check(require(detail::visit_effective_lower_cockpit_contact(
                      kept, [](const auto&) { return false; }))
                  .visited_triangles == 1,
          "Moved-to contact retains immutable source storage");
    return require(detail::boarding_source_endpoint_surface_checkpoint_bounded(
        retained, 10, 1, 105, 14, 10, 1));
  }();
  const auto saved = snapshot(d);
  auto copy = d;
  auto moved = std::move(d);
  check(snapshot(copy) == saved && snapshot(moved) == saved,
        "Owned source names and complete evidence survive copy/move and local "
        "provider destruction");
  if (moved.surface.first_refusal && moved.surface.first_refusal->pair_index) {
    const auto& r = *moved.surface.first_refusal;
    const auto t =
        source_at(*moved.load.self.endpoint.sites.source.contact(),
                  *r.pair_index % moved.surface.effective_triangle_count);
    check(r.source_object == t.source_object,
          "Child-backed stopping name view survives owning result move");
  }
  if (copy.load.self.endpoint.body)
    copy.load.self.endpoint.body->points[0].lower.x = 8;
  copy.surface.complete = true;
  copy.surface.surface_qualified = true;
  check(snapshot(moved) == saved, "Mutable returned evidence cannot modify "
                                  "sharing immutable original source");
  const auto fresh =
      require(detail::boarding_source_endpoint_surface_checkpoint_bounded(
          moved.load.self.endpoint.sites.source, 10, 1, 105, 14, 10, 1));
  check(snapshot(fresh) == saved,
        "Fresh bounded assessment ignores mutable copied report fields");
}
} // namespace
int main() {
  try {
    static_assert(sizeof(BoardingSourceEndpointSurfaceCheckpointPayload) <=
                  4096);
    static_assert(!Pair::surface_qualified && !Pair::body_qualified &&
                  !Pair::world_qualified);
    if (std::numeric_limits<long double>::digits < 64)
      throw std::runtime_error("Independent oracle requires64 mantissa bits");
    const auto binding = require(make_native_starting_assembly_binding(
        NativeStartingAssemblySelection{}));
    const auto provider = require(make_origin_boarding_boot_support(binding));
    invalid_controls();
    support_controls();
    pair_controls();
    sole_controls();
    environment_controls(provider);
    child_capacity_controls(provider);
    const auto* contact = provider.contact();
    check(contact != nullptr,
          "Source-bound provider retains selected effective contact");
    if (!contact) throw std::runtime_error("Missing source-bound contact");
    crop_controls(*contact);
    const auto inv = inventory(*contact);
    for (std::uint64_t position : std::array<std::uint64_t, 4>{
             inv.counts[0] - 1, inv.counts[0], inv.counts[0] + inv.counts[1],
             inv.total - 1}) {
      const auto entry = source_at(*contact, position);
      const auto expected = position < inv.counts[0]
                                ? LowerCockpitContactBuffer::original
                            : position < inv.counts[0] + inv.counts[1]
                                ? LowerCockpitContactBuffer::halo
                                : LowerCockpitContactBuffer::replacement;
      check(entry.key.buffer == expected, "Stopped namespace-transition/final "
                                          "prefixes retain whole denominator");
    }
    const auto unchanged = snapshot(
        require(assess_origin_boarding_source_endpoint_load(provider)));
    const auto actual = observe(provider);
    const auto independent = oracle();
    check(snapshot(actual.load) == unchanged,
          "New checkpoint owns unmodified complete Load/Self/Endpoint/Sites "
          "snapshots");
    original_shape_controls(actual.load, independent);
    inspect_surface(actual, inv, independent,
                    kBoardingSourceEndpointSurfaceMaximumPairs, 16);
    prefix_controls(provider, inv, independent, unchanged);
    lifetime_controls(binding);
    check(snapshot(require(assess_origin_boarding_source_endpoint_load(
              provider))) == unchanged,
          "Surface math/prefix/lifetime controls leave original child provider "
          "unchanged");
  } catch (const std::exception& e) {
    ++failures;
    std::cerr << "EXCEPTION: " << e.what() << '\n';
  }
  std::cout << checks << " fixed endpoint surface checks, " << failures
            << " failures\n";
  return failures == 0 ? 0 : 1;
}
