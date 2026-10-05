#include "apsis_drift/origin_boarding_lower_foot_transfer_surface_sweep.hpp"
#include "origin_boarding_lower_foot_transfer_surface_sweep_internal.hpp"
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
#include <span>
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
using Sweep = BoardingLowerFootTransferSurfaceSweepDiagnostic;
using Payload = BoardingLowerFootTransferSurfaceSweepPayload;
using Surface = BoardingSourceEndpointSurfaceCheckpointDiagnostic;
using Limits = detail::BoardingLowerFootTransferSurfaceSweepLimits;
using Condition = BoardingLowerFootTransferSurfaceSweepCondition;
using Solid = detail::BoardingSourceEndpointSurfaceCheckpointSolidBounds;
using Shape = detail::BoardingSourceEndpointSurfaceCheckpointShape;
using Triangle = std::array<RigidVector3, 3>;
using Math = detail::BoardingLowerFootTransferSurfaceSweepMathEvidence;
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
    throw std::runtime_error("Required surface-sweep API refused input");
  return std::move(*value);
}
auto component(RigidVector3 p, std::size_t axis) -> double {
  return axis == 0 ? p.x : axis == 1 ? p.y : p.z;
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
auto snapshot(const Sweep& d) -> Snapshot {
  auto s = snapshot(d.transfer);
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
           p.effective_triangle_count, p.cell_count, p.expected_base_pairs,
           p.logical_expected_comparisons, p.logical_certified_comparisons,
           p.examined_base_pairs, p.completed_base_pairs,
           p.union_broad_certified_pairs, p.refined_completed_base_pairs,
           p.examined_cell_pairs, p.certified_cell_pairs,
           p.cell_broad_certified_pairs, p.cell_sole_certified_pairs,
           p.cell_support_certified_pairs, p.axes_examined, p.unsupported_axes,
           p.visited_triangles, p.metadata_visited_triangles})
    s.integer(n);
  for (bool b : std::array{p.arithmetic_supported, p.coverage_complete,
                           p.comparisons_complete,
                           p.partial_continuous_surface_qualified, p.complete})
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
    s.integer(r.base_pair_index.has_value());
    if (r.base_pair_index) s.integer(*r.base_pair_index);
    s.integer(r.cell_index.has_value());
    if (r.cell_index) s.integer(*r.cell_index);
    s.integer(r.evaluated_source_triangle.has_value());
    if (r.evaluated_source_triangle) s.integer(*r.evaluated_source_triangle);
    s.integer(r.actual_triangle_metres.has_value());
    if (r.actual_triangle_metres)
      for (auto point : *r.actual_triangle_metres)
        s.point(point);
    s.number(r.first);
    s.number(r.last);
    s.integer(r.axes_examined);
    s.integer(r.unsupported_axes);
    s.integer(r.object);
    s.integer(r.part);
    s.integer(r.condition);
  }
  return s;
}
auto box(RigidVector3 low = {}, RigidVector3 high = {}) -> Solid {
  return {{low, high}, {low, high}, {.1, .1, .1}, 0, Shape::box};
}
auto plane_x(double x, double y = 0, double z = 0) -> Triangle {
  return {{{x, y - .05, z - .05}, {x, y + .05, z - .05}, {x, y, z + .05}}};
}
auto math(std::span<const Solid> cells, std::span<const Triangle> triangles,
          Limits limits = {}) -> Math {
  return require(detail::boarding_lower_foot_transfer_surface_sweep_math(
      cells, 0, triangles, limits));
}
auto no_authority(const Sweep& d) -> void {
  check(!d.world_qualified && !d.volume_qualified && !d.material_qualified &&
            !d.strength_qualified && !d.dynamics_qualified &&
            !d.friction_qualified && !d.route_qualified && !d.actor_qualified &&
            !d.seat_qualified && !d.save_qualified &&
            !d.first_flight_qualified && !d.foot_acquisition_qualified &&
            !d.free_foot_swing_qualified &&
            !d.complete_lower_step_to_seat_transfer_qualified,
        "Continuous triangle surfaces grant no volume, force or actor "
        "permission");
}
auto accounting(const Payload& p, const Limits& limits, bool source = true)
    -> void {
  check(p.examined_base_pairs <= limits.base_pairs &&
            p.examined_cell_pairs <= limits.refined_pairs &&
            p.axes_examined <= limits.axes &&
            p.unsupported_axes <= p.axes_examined &&
            p.axes_examined <= 16 * p.examined_cell_pairs,
        "Real performed base/refinement/axis prefixes respect independent "
        "registered ceilings");
  check(p.completed_base_pairs == p.union_broad_certified_pairs +
                                      p.refined_completed_base_pairs &&
            p.completed_base_pairs <= p.examined_base_pairs &&
            p.certified_cell_pairs <= p.examined_cell_pairs,
        "Actual complete bases and cell proofs remain separate from attempted "
        "stopping work");
  check(p.certified_cell_pairs == p.cell_broad_certified_pairs +
                                      p.cell_sole_certified_pairs +
                                      p.cell_support_certified_pairs,
        "Every actual cell certificate belongs to exactly one proof family");
  check(p.logical_certified_comparisons ==
            p.union_broad_certified_pairs * p.cell_count +
                p.certified_cell_pairs,
        "Logical coverage counts skipped union proofs without claiming "
        "executed cell math");
  if (p.effective_triangle_count != 0 && p.cell_count != 0)
    check(p.logical_expected_comparisons ==
              p.expected_base_pairs * p.cell_count,
          "Complete logical denominator uses checked64-bit base times "
          "ownedcell count");
  if (p.complete)
    check(p.arithmetic_supported && p.coverage_complete == source &&
              p.comparisons_complete &&
              p.partial_continuous_surface_qualified == source &&
              !p.first_refusal &&
              p.examined_base_pairs == p.expected_base_pairs &&
              p.completed_base_pairs == p.expected_base_pairs &&
              p.logical_certified_comparisons ==
                  p.logical_expected_comparisons &&
              p.examined_cell_pairs == p.certified_cell_pairs,
          "Whole completion requires crop, every base and every necessary "
          "closedcell proof");
  else
    check(!p.partial_continuous_surface_qualified && !p.comparisons_complete &&
              p.first_refusal,
          "An incomplete prefix retains a refusal and cannot manufacture "
          "whole-sweep permission");
}
auto numeric_fixtures() -> void {
  static_assert(!Math::body_qualified && !Math::source_qualified &&
                !Math::surface_qualified && !Math::world_qualified &&
                !Math::volume_qualified && !Math::actor_qualified &&
                !Math::route_qualified);
  const std::array around{box({-2, 0, 0}, {-.2, 0, 2}),
                          box({-.2, 0, 2}, {.2, 0, 2}),
                          box({.2, 0, 0}, {2, 0, 2})};
  const std::array<Triangle, 1> first{plane_x(0)};
  const auto clear = math(around, first);
  accounting(clear.work, Limits{}, false);
  check(clear.work.complete && clear.work.union_broad_certified_pairs == 0 &&
            clear.work.refined_completed_base_pairs == 1 &&
            clear.work.examined_cell_pairs == 3 &&
            clear.work.certified_cell_pairs == 3 &&
            clear.work.axes_examined == 0,
        "Unresolved whole union reaches three individually clear genuine cell "
        "enclosures");
  const std::array consecutive{first[0], plane_x(-1, 0, 1)};
  const auto next = math(around, consecutive);
  accounting(next.work, Limits{}, false);
  check(!next.work.complete && next.work.examined_base_pairs == 2 &&
            next.work.completed_base_pairs == 1 &&
            next.work.union_broad_certified_pairs == 0 &&
            next.work.examined_cell_pairs == 4 &&
            next.work.certified_cell_pairs == 3 && next.work.first_refusal &&
            next.work.first_refusal->cell_index == 0 &&
            next.work.first_refusal->base_pair_index == 1 &&
            next.work.first_refusal->condition ==
                Condition::no_separation_certificate,
        "NEXT triangle restores immutable wholeunion and cannot reuse previous "
        "farcell shortcut");
  const std::array middle{box({-2, 0, 0}, {-2, 0, 0}), box(),
                          box({2, 0, 0}, {2, 0, 0})};
  const auto crossing = math(middle, first);
  accounting(crossing.work, Limits{}, false);
  check(!crossing.work.complete && crossing.work.first_refusal &&
            crossing.work.first_refusal->cell_index == 1 &&
            crossing.work.certified_cell_pairs == 1 &&
            crossing.work.examined_cell_pairs == 2,
        "Clear endpoints cannot certify a triangle crossing in the "
        "intermediate cell");
  const std::array<Triangle, 1> far{plane_x(4)};
  Limits no_refinement;
  no_refinement.refined_pairs = 0;
  no_refinement.axes = 0;
  const auto broad = math(around, far, no_refinement);
  accounting(broad.work, no_refinement, false);
  check(broad.work.complete && broad.work.union_broad_certified_pairs == 1 &&
            broad.work.examined_cell_pairs == 0 &&
            broad.work.logical_certified_comparisons == 3,
        "Whole union can certify allcells with zero refined work allowance");
  for (auto item :
       std::array{std::pair{Condition::source_capacity, 0U},
                  std::pair{Condition::base_pair_capacity, 1U},
                  std::pair{Condition::refined_pair_capacity, 2U}}) {
    Limits limit;
    if (item.second == 0) limit.source_triangles = 0;
    if (item.second == 1) limit.base_pairs = 0;
    if (item.second == 2) limit.refined_pairs = 0;
    const auto d = math(around, first, limit);
    accounting(d.work, limit, false);
    check(!d.work.complete && d.work.first_refusal &&
              d.work.first_refusal->condition == item.first &&
              d.work.examined_cell_pairs == 0 && d.work.axes_examined == 0,
          "Reached synthetic source/base/refinement zero cap stops before next "
          "unattempted operation");
  }
  Limits exact;
  exact.source_triangles = 1;
  exact.base_pairs = 1;
  exact.refined_pairs = 3;
  exact.axes = 0;
  const auto exact_clear = math(around, first, exact);
  check(exact_clear.work.complete && exact_clear.work.examined_cell_pairs == 3,
        "Exact genuine refined-cell denominator completes without a spare "
        "budget unit");
  exact.refined_pairs = 2;
  const auto one_less = math(around, first, exact);
  check(one_less.work.first_refusal &&
            one_less.work.first_refusal->condition ==
                Condition::refined_pair_capacity &&
            one_less.work.examined_cell_pairs == 2 &&
            one_less.work.certified_cell_pairs == 2 &&
            one_less.work.first_refusal->cell_index == 2,
        "One-less refined budget retains correct nextunattempted cell and "
        "exact prefix");
  const std::array one{box()};
  const std::array<Triangle, 1> diagonal{
      Triangle{{{1, 0, 0}, {0, 1, 0}, {1, 1, 0}}}};
  const auto supported = math(one, diagonal);
  accounting(supported.work, Limits{}, false);
  check(supported.work.complete && supported.work.examined_cell_pairs == 1 &&
            supported.work.cell_support_certified_pairs == 1 &&
            supported.work.axes_examined > 0,
        "Diagonal clear triangle reaches actual separating support axes beyond "
        "overlapping AABBs");
  for (bool aggregate : std::array{false, true}) {
    Limits zero;
    if (aggregate)
      zero.axes = 0;
    else
      zero.pair_axes = 0;
    const auto d = math(one, diagonal, zero);
    check(d.work.first_refusal &&
              d.work.first_refusal->condition ==
                  (aggregate ? Condition::aggregate_axis_capacity
                             : Condition::pair_axis_capacity) &&
              d.work.examined_cell_pairs == 1 && d.work.axes_examined == 0,
          "Reached zero aggregate/perpair axis allowance stops before first "
          "actual axis trial");
  }
  if (supported.work.complete && supported.work.axes_examined > 0) {
    Limits exact_axes;
    exact_axes.axes = supported.work.axes_examined;
    const auto equality = math(one, diagonal, exact_axes);
    check(equality.work.complete &&
              equality.work.axes_examined == supported.work.axes_examined,
          "Exact actual axis budget preserves positive arithmetic fixture "
          "certificate");
    --exact_axes.axes;
    const auto less = math(one, diagonal, exact_axes);
    check(!less.work.complete && less.work.first_refusal &&
              less.work.first_refusal->condition ==
                  Condition::aggregate_axis_capacity &&
              less.work.axes_examined == exact_axes.axes,
          "One-less actual aggregate budget refuses before exceeding performed "
          "axis allowance");
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
    check(std::fesetenv(&saved) == 0, "Restore exact arithmetic environment");
  }
};
auto unsafe_environment(const OriginBoardingBootSupport& source) -> void {
  const std::array cells{box()};
  const std::array<Triangle, 1> triangles{plane_x(4)};
  const auto bounds =
      detail::boarding_lower_foot_transfer_surface_union_bounds_math(cells[0],
                                                                     0);
  check(!bounds || (!bounds->arithmetic_supported && !bounds->examined),
        "Unsafe environment refuses even obvious numeric union separation");
  const auto e = detail::boarding_lower_foot_transfer_surface_sweep_math(
      cells, 0, triangles);
  check(!e || (!e->work.complete && !e->work.arithmetic_supported &&
               e->work.axes_examined == 0),
        "Two-stage fixture refuses unsafe arithmetic before broadshortcut "
        "permission");
  Limits l;
  l.child.nodes = 0;
  const auto d = detail::boarding_lower_foot_transfer_surface_sweep_bounded(
      source, 0, 1, l);
  check(!d || (!d->surface.complete && !d->surface.arithmetic_supported &&
               d->surface.examined_base_pairs == 0 &&
               d->surface.examined_cell_pairs == 0 &&
               d->surface.axes_examined == 0),
        "Public source path cannot waive unsafe environment through zero "
        "childwork cap");
}
auto environment_controls(const OriginBoardingBootSupport& source) -> void {
  for (int mode : std::array{FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO}) {
    SavedEnvironment saved;
    check(std::fesetround(mode) == 0, "Install directed rounding adversary");
    unsafe_environment(source);
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
    unsafe_environment(source);
  }
  for (unsigned mode : std::array{1U, 2U, 3U}) {
    SavedEnvironment saved;
    SavedControl control;
    _mm_setcsr((control.value & ~(3U << 13)) | (mode << 13));
    unsafe_environment(source);
  }
#endif
  check(std::fegetround() == FE_TONEAREST,
        "RN restored before first full surface observation");
}
auto invalid_controls(const OriginBoardingBootSupport& source) -> void {
  const auto nan = std::numeric_limits<double>::quiet_NaN(),
             inf = std::numeric_limits<double>::infinity();
  for (double v : std::array{nan, inf, -inf, -.001, 1.001})
    check(!assess_origin_boarding_lower_foot_transfer_surface_sweep(source, v,
                                                                    .5) &&
              !assess_origin_boarding_lower_foot_transfer_surface_sweep(source,
                                                                        .5, v),
          "Public endpoint input must be finite inside closed[0,1]");
  auto handle = source;
  auto live = std::move(handle);
  // NOLINTBEGIN(bugprone-use-after-move) -- Empty-handle API contract.
  check(!assess_origin_boarding_lower_foot_transfer_surface_sweep(handle),
        "Moved-empty source refuses public assessment");
  // NOLINTEND(bugprone-use-after-move) -- Empty-handle API contract.
  check(live.contact() == source.contact(),
        "Live moved provider keeps actual immutable source");
  const auto too_large = [&](Limits l) {
    check(!detail::boarding_lower_foot_transfer_surface_sweep_bounded(source, 0,
                                                                      1, l),
          "Private capacities can only lower registered ceilings");
  };
  Limits l;
  l.source_triangles = kBoardingLowerFootTransferSurfaceMaximumTriangles + 1;
  too_large(l);
  l = {};
  l.base_pairs = kBoardingLowerFootTransferSurfaceMaximumBasePairs + 1;
  too_large(l);
  l = {};
  l.refined_pairs = kBoardingLowerFootTransferSurfaceMaximumRefinedPairs + 1;
  too_large(l);
  l = {};
  l.axes = kBoardingLowerFootTransferSurfaceMaximumAxes + 1;
  too_large(l);
  l = {};
  l.pair_axes = kBoardingLowerFootTransferSurfaceMaximumPairAxes + 1;
  too_large(l);
  l = {};
  l.child.nodes = kBoardingLowerFootTransferMaximumNodes + 1;
  too_large(l);
  const std::array cells{box()};
  const std::array<Triangle, 1> triangles{plane_x(4)};
  check(!detail::boarding_lower_foot_transfer_surface_sweep_math({}, 0,
                                                                 triangles) &&
            !detail::boarding_lower_foot_transfer_surface_sweep_math(cells, 0,
                                                                     {}),
        "Empty arithmetic fixtures cannot grant a vacuous sweep");
  for (std::size_t kind = 0; kind < 4; ++kind) {
    auto bad = cells;
    if (kind == 0) bad[0].first.lower.x = nan;
    if (kind == 1) bad[0].first.lower.x = 1;
    if (kind == 2) bad[0].half_size_metres.x = 0;
    if (kind == 3) bad[0].radius_metres = 1;
    check(!detail::boarding_lower_foot_transfer_surface_sweep_math(bad, 0,
                                                                   triangles),
          "Malformed or incompatible solidbounds refuse before positive kernel "
          "math");
  }
  for (std::size_t vertex = 0; vertex < 3; ++vertex) {
    auto bad = triangles;
    bad[0][vertex].y = nan;
    check(
        !detail::boarding_lower_foot_transfer_surface_sweep_math(cells, 0, bad),
        "Every source triangle vertex must be finite");
  }
  const auto coverage = require(
      detail::boarding_lower_foot_transfer_surface_union_bounds_math(box(), 0));
  auto invalid = coverage;
  invalid.upper_metres.x = invalid.lower_metres.x - 1;
  check(!detail::boarding_lower_foot_transfer_surface_union_broad(invalid,
                                                                  triangles[0]),
        "Malformed cached union bounds cannot create a broadshortcut");
  auto stale = coverage;
  stale.arithmetic_supported = false;
  check(
      !detail::boarding_lower_foot_transfer_surface_union_broad(stale,
                                                                triangles[0]),
      "Stale unsupported union record cannot manufacture shortcut permission");
  for (auto v : std::array{nan, inf})
    check(!detail::boarding_lower_foot_transfer_surface_union_bounds_math(box(),
                                                                          v),
          "Nonfinite commonplacement refuses before frame transformation");
  auto capsule = Solid{
      {{0, 0, 0}, {0, 0, 0}}, {{1, 0, 0}, {1, 0, 0}}, {}, .2, Shape::capsule};
  const auto b =
      require(detail::boarding_lower_foot_transfer_surface_union_bounds_math(
          capsule, .5));
  check(b.arithmetic_supported && b.lower_metres.x <= -.2 &&
            b.upper_metres.x >= 1.2 && b.lower_metres.y <= .3 &&
            b.upper_metres.y >= .7,
        "Independent capsule AABB retains radius around both ends and commonY "
        "once");
  const auto touch = plane_x(coverage.upper_metres.x);
  check(require(detail::boarding_lower_foot_transfer_surface_union_broad(
                    coverage, touch))
            .certified,
        "Closed AABB boundary supports unchanged open-solid separation "
        "convention");
}
auto child_cap_controls(const OriginBoardingBootSupport& source) -> void {
  Limits l;
  l.child.nodes = 0;
  const auto standalone = require(
      detail::boarding_lower_foot_transfer_bounded(source, 0, 1, l.child));
  const auto d =
      require(detail::boarding_lower_foot_transfer_surface_sweep_bounded(
          source, 0, 1, l));
  check(snapshot(d.transfer) == snapshot(standalone),
        "Refused child is retained completely fieldwise without replacement");
  check(d.surface.first_refusal &&
            d.surface.first_refusal->condition ==
                Condition::child_prerequisite &&
            d.surface.examined_base_pairs == 0 &&
            d.surface.examined_cell_pairs == 0 && d.surface.axes_examined == 0,
        "Child capacity refusal stops before any surface or crop comparison");
  no_authority(d);
}
auto observe(const OriginBoardingBootSupport& source) -> Sweep {
  auto d =
      require(assess_origin_boarding_lower_foot_transfer_surface_sweep(source));
  const auto& p = d.surface;
  std::cout << std::setprecision(17)
            << "FIRST lower-foot source-surface sweep version="
            << p.surface_version << " child=" << d.transfer.complete
            << " arithmetic=" << p.arithmetic_supported
            << " coverage=" << p.coverage_complete
            << " comparisons=" << p.comparisons_complete
            << " surface=" << p.partial_continuous_surface_qualified
            << " complete=" << p.complete
            << " triangles=" << p.effective_triangle_count
            << " cells=" << p.cell_count
            << " expected_base=" << p.expected_base_pairs
            << " logical_expected=" << p.logical_expected_comparisons
            << " logical_certified=" << p.logical_certified_comparisons
            << " base=" << p.examined_base_pairs
            << " completed_base=" << p.completed_base_pairs
            << " union=" << p.union_broad_certified_pairs
            << " refined_base=" << p.refined_completed_base_pairs
            << " cell_pairs=" << p.examined_cell_pairs
            << " cell_certified=" << p.certified_cell_pairs
            << " cell_broad=" << p.cell_broad_certified_pairs
            << " sole=" << p.cell_sole_certified_pairs
            << " support=" << p.cell_support_certified_pairs
            << " axes=" << p.axes_examined
            << " unsupported=" << p.unsupported_axes
            << " visits=" << p.visited_triangles
            << " metadata_visits=" << p.metadata_visited_triangles
            << " world=" << d.world_qualified
            << " volume=" << d.volume_qualified << '\n';
  if (p.first_refusal) {
    const auto& r = *p.first_refusal;
    std::cout << "REFUSAL condition=" << static_cast<unsigned>(r.condition)
              << " part=" << static_cast<unsigned>(r.part) << " base="
              << (r.base_pair_index ? static_cast<long long>(*r.base_pair_index)
                                    : -1LL)
              << " cell="
              << (r.cell_index ? static_cast<long long>(*r.cell_index) : -1LL)
              << " interval=[" << r.first << ',' << r.last << ']'
              << " axes=" << r.axes_examined
              << " unsupported=" << r.unsupported_axes;
    if (r.key)
      std::cout << " key=" << static_cast<unsigned>(r.key->buffer) << ':'
                << r.key->group << ':' << r.key->triangle;
    std::cout << " object=" << r.object << " name=" << r.source_object << '\n';
  } else
    std::cout << "REFUSAL none\n";
  for (const auto& c : p.coverage)
    if (c.examined)
      std::cout << "CROP part=" << static_cast<unsigned>(c.part)
                << " covered=" << c.covered << " min=[" << c.lower_metres.x
                << ',' << c.lower_metres.y << ',' << c.lower_metres.z
                << "] max=[" << c.upper_metres.x << ',' << c.upper_metres.y
                << ',' << c.upper_metres.z << "]\n";
  std::cout.flush();
  return d;
}
auto inspect_union(const Sweep& d) -> void {
  no_authority(d);
  accounting(d.surface, Limits{});
  const auto& child = d.transfer;
  if (!child.complete) return;
  check(child.seconds_per_parameter == 4 &&
            child.common_translation_y_metres == .847,
        "Surface consumer retains exact original childclock and one "
        "commonplacement");
  auto cursor = std::min(child.requested_first, child.requested_last);
  std::array<BoardingPlantedLegPointBounds, 18> united{};
  bool first = true;
  for (const auto& cell : child.cells) {
    check(cell.complete && cell.first == cursor && cell.first <= cell.last,
          "Consumer uses unchanged exact ascending closedchild cover without "
          "resampling");
    cursor = cell.last;
    for (std::size_t i = 0; i < united.size(); ++i) {
      const auto& p = cell.points[i].value;
      auto& u = united[i];
      if (first)
        u = p;
      else {
        u.lower = {std::min(u.lower.x, p.lower.x),
                   std::min(u.lower.y, p.lower.y),
                   std::min(u.lower.z, p.lower.z)};
        u.upper = {std::max(u.upper.x, p.upper.x),
                   std::max(u.upper.y, p.upper.y),
                   std::max(u.upper.z, p.upper.z)};
      }
    }
    first = false;
  }
  check(!first &&
            cursor == std::max(child.requested_first, child.requested_last),
        "Owned child's complete closed cover is authoritative for all18 point "
        "unions");
  const auto ctx =
      require(detail::prepare_boarding_lower_foot_transfer_surface(child));
  const auto* contact = child.initial.self.endpoint.sites.source.contact();
  check(contact != nullptr, "Owned initial retains immutable sourcecontact");
  std::size_t boxes{}, capsules{};
  for (std::size_t i = 0; i < child.parts.size(); ++i) {
    const auto& binding = child.parts[i];
    check(static_cast<std::size_t>(binding.id) == i,
          "Every original WORLD part remains in exact enumorder");
    const auto coverage = require(
        detail::boarding_lower_foot_transfer_surface_union_bounds(ctx, i));
    check(coverage.examined && coverage.arithmetic_supported,
          "Actual union consumes finite supported original pointbounds");
    BoardingPlantedLegPointBounds a, b;
    RigidVector3 half{};
    double radius{};
    if (const auto* shape =
            std::get_if<BoardingPlantedBodyBoxBinding>(&binding.reservation)) {
      ++boxes;
      a = b = united[static_cast<std::size_t>(shape->center)];
      half = shape->half_size_metres;
      check(shape->frame.columns == BoardingBodyFrame{}.columns,
            "Full WORLD boxes retain original identityframes");
      if (i == 1)
        check(half == RigidVector3{.26, .2695, .18},
              "WORLD trunk stays fullbox rather than SELF ellipsoid");
      if (i == 2)
        check(half == RigidVector3{.16, .18, .18},
              "WORLD helmet stays fullbox rather than SELF ellipsoid");
    } else {
      ++capsules;
      const auto& capsule =
          std::get<BoardingPlantedBodyCapsuleBinding>(binding.reservation);
      a = united[static_cast<std::size_t>(capsule.start)];
      b = united[static_cast<std::size_t>(capsule.end)];
      radius = capsule.radius_metres;
    }
    for (std::size_t axis = 0; axis < 3; ++axis) {
      const auto extent = static_cast<long double>(
          radius == 0 ? component(half, axis) : radius);
      const auto placement =
          axis == 1
              ? static_cast<long double>(child.common_translation_y_metres)
              : 0.L;
      auto low = static_cast<long double>(std::min(component(a.lower, axis),
                                                   component(b.lower, axis))) -
                 extent + placement;
      const auto high =
          static_cast<long double>(
              std::max(component(a.upper, axis), component(b.upper, axis))) +
          extent + placement;
      if ((i == 5 || i == 11) && axis == 1)
        low = static_cast<long double>(
            child.initial.self.endpoint.legs[i == 5 ? 0 : 1].plane_metres);
      const auto rounding =
          65536 * std::numeric_limits<long double>::epsilon() *
          std::max(1.L, std::max(std::abs(low), std::abs(high)));
      check(static_cast<long double>(component(coverage.lower_metres, axis)) <=
                    low + rounding &&
                static_cast<long double>(
                    component(coverage.upper_metres, axis)) >= high - rounding,
            "Independent fullshape support encloses every sourcechildcell with "
            "commonY once");
    }
    if (i == 5 || i == 11)
      check(coverage.lower_metres.y ==
                child.initial.self.endpoint.legs[i == 5 ? 0 : 1].plane_metres,
            "Authenticated stationary sole keeps exact actual productplane "
            "before cropguard");
    const auto& reported = d.surface.coverage[i];
    if (reported.examined) {
      check(reported.lower_metres == coverage.lower_metres &&
                reported.upper_metres == coverage.upper_metres &&
                reported.part == coverage.part,
            "Reported immutable wholeunion cannot be overwritten by a narrower "
            "lastcell Workspace");
      if (contact)
        check(reported.covered == require(detail::covers_lower_cockpit_bounds(
                                      *contact, {coverage.lower_metres,
                                                 coverage.upper_metres})),
              "Actual union coverage uses original immutable crop and notch");
    }
  }
  check(boxes == 7 && capsules == 8,
        "Continuous WORLD surface roster retains all15 full reservations");
  if (d.surface.examined_base_pairs > 0) {
    check(d.surface.coverage_complete &&
              std::ranges::all_of(d.surface.coverage,
                                  [](const auto& c) {
                                    return c.examined &&
                                           c.arithmetic_supported && c.covered;
                                  }),
          "Every actual motionunion cropguard precedes the first sourcepair "
          "comparison");
  }
  if (d.surface.first_refusal &&
      d.surface.first_refusal->condition == Condition::crop_uncovered)
    check(!d.surface.coverage_complete && d.surface.examined_base_pairs == 0 &&
              d.surface.examined_cell_pairs == 0 &&
              d.surface.axes_examined == 0,
          "Conservative unioncrop refusal performs no hidden pairmath or "
          "adaptive repair");
  if (d.surface.effective_triangle_count > 0)
    check(d.surface.expected_base_pairs ==
              15 * d.surface.effective_triangle_count,
          "Actual source denominator accounts for all15 original part/source "
          "pairs");
}
auto source_bridge_and_sole_controls(const Diagnostic& child) -> void {
  const auto ctx =
      require(detail::prepare_boarding_lower_foot_transfer_surface(child));
  const auto* contact = child.initial.self.endpoint.sites.source.contact();
  if (!contact) throw std::runtime_error("Missing original contact");
  std::optional<detail::LowerCockpitEffectiveTriangle> far;
  const auto visited = require(detail::visit_effective_lower_cockpit_contact(
      *contact, [&](const auto& triangle) {
        if (!triangle.obstacle) return true;
        for (const auto& point : triangle.obstacle->points)
          for (std::size_t axis = 0; axis < 3; ++axis)
            if (std::abs(component(point, axis)) > 8) {
              far = triangle;
              return false;
            }
        return true;
      }));
  check(visited.total_triangles > 0, "Immutable originalsource traversal "
                                     "retains complete catalog denominator");
  if (far) {
    const auto e =
        detail::boarding_lower_foot_transfer_surface_cell_pair(ctx, 0, 0, *far);
    check(e && e->arithmetic_supported,
          "Actual finite originalsource triangle above private8m limit reaches "
          "genuine bridge");
    const auto& shape =
        std::get<BoardingPlantedBodyBoxBinding>(child.parts[0].reservation);
    const auto center =
        child.cells[0].points[static_cast<std::size_t>(shape.center)].value;
    const Solid local{center, center, shape.half_size_metres, 0, Shape::box};
    check(!detail::boarding_source_endpoint_surface_checkpoint_pair_math(
              local, child.common_translation_y_metres, far->obstacle->points),
          "Arbitrary local numeric triangle seam retains its8m boundary "
          "without blocking actual originalsource");
  } else
    std::cout << "CONTROL original fartriangle absent; finite-only bridge "
                 "tested with private arithmetic object\n";
  // A private bridge call returns arithmetic evidence, never source enrollment.
  detail::CabinContactObstacle obstacle{};
  obstacle.points = plane_x(20);
  detail::LowerCockpitEffectiveTriangle triangle{
      .obstacle = &obstacle,
      .key = {},
      .object = 0,
      .source_object = "arithmetic-only",
      .evaluated_source_triangle = {}};
  check(require(detail::boarding_lower_foot_transfer_surface_cell_pair(
                    ctx, 0, 0, triangle))
            .certified,
        "Finite-only bridge can certify distant arithmetic triangle without "
        "truncating originalsource domain");
  const auto plane = child.initial.self.endpoint.legs[0].plane_metres;
  const auto x = static_cast<double>(static_cast<long double>(.16) -
                                     static_cast<long double>(.14));
  obstacle.points = {
      {{x - .01, plane, -.51}, {x + .01, plane, -.51}, {x, plane, -.49}}};
  check(require(detail::boarding_lower_foot_transfer_surface_cell_pair(
                    ctx, 5, 0, triangle))
            .certified,
        "Authentic stationary boot excludes all three source vertices on exact "
        "soleplane");
  obstacle.points[2].y = plane + .001;
  check(!require(detail::boarding_lower_foot_transfer_surface_cell_pair(
                     ctx, 5, 0, triangle))
             .certified,
        "One sourcevertex above sole forbids blanket floorcontact exemption");
  obstacle.points[2].y = std::numeric_limits<double>::quiet_NaN();
  check(!detail::boarding_lower_foot_transfer_surface_cell_pair(ctx, 5, 0,
                                                                triangle),
        "Nonfinite sole/source vertex cannot pass an exact identity shortcut");
  auto malformed = child;
  malformed.common_translation_y_metres = .72;
  check(!detail::prepare_boarding_lower_foot_transfer_surface(malformed),
        "Altered commonframe cannot enroll authentic soletemplate");
  malformed = child;
  malformed.initial.self.endpoint.legs[0].boot_center_y_terms[2] = -.72;
  check(!detail::prepare_boarding_lower_foot_transfer_surface(malformed),
        "Rounded altered soleexpression cannot enroll originalsource contact");
  malformed = child;
  malformed.cells[0].points[7].derivatives.velocity.lower.x = 1;
  check(!detail::prepare_boarding_lower_foot_transfer_surface(malformed),
        "Nonstationary originalboot cannot retain fixedsole identity");
  malformed = child;
  malformed.cells[0].points[0].value.lower.x = 9;
  check(!detail::prepare_boarding_lower_foot_transfer_surface(malformed),
        "Malformed owned pointbounds refuse consumer context");
  malformed = child;
  malformed.cells.front().first = .25;
  check(
      !detail::prepare_boarding_lower_foot_transfer_surface(malformed),
      "Changed childcover boundary cannot grant continuous surface permission");
  check(!detail::boarding_lower_foot_transfer_surface_union_bounds(ctx, 15) &&
            !detail::boarding_lower_foot_transfer_surface_cell_pair(
                ctx, 0, child.cells.size(), triangle),
        "Borrowed context enforces original part and retainedcell boundaries");
}
auto production_caps(const OriginBoardingBootSupport& source,
                     const Sweep& actual) -> void {
  for (std::size_t kind = 0; kind < 5; ++kind) {
    Limits l;
    if (kind == 0) l.source_triangles = 0;
    if (kind == 1) l.base_pairs = 0;
    if (kind == 2) l.refined_pairs = 0;
    if (kind == 3) l.axes = 0;
    if (kind == 4) l.pair_axes = 0;
    const auto d =
        require(detail::boarding_lower_foot_transfer_surface_sweep_bounded(
            source, 0, 1, l));
    no_authority(d);
    accounting(d.surface, l);
    check(snapshot(d.transfer) == snapshot(actual.transfer),
          "Lowered new surface budgets leave whole owned447 child exactly "
          "unchanged");
    if (kind == 0)
      check(d.surface.first_refusal &&
                d.surface.first_refusal->condition ==
                    Condition::source_capacity &&
                d.surface.examined_base_pairs == 0,
            "Reached zero original source-total allowance stops before first "
            "base comparison");
    if (kind == 1 && d.surface.coverage_complete)
      check(d.surface.first_refusal &&
                d.surface.first_refusal->condition ==
                    Condition::base_pair_capacity &&
                d.surface.examined_base_pairs == 0 &&
                d.surface.examined_cell_pairs == 0,
            "Reached zero base allowance names next unattempted sourcepair "
            "after all unioncrop guards");
  }
  if (!actual.surface.complete) return;
  Limits exact;
  exact.source_triangles = actual.surface.effective_triangle_count;
  exact.base_pairs = actual.surface.examined_base_pairs;
  exact.refined_pairs = actual.surface.examined_cell_pairs;
  exact.axes = actual.surface.axes_examined;
  const auto replay =
      require(detail::boarding_lower_foot_transfer_surface_sweep_bounded(
          source, 0, 1, exact));
  check(snapshot(replay) == snapshot(actual),
        "Observed exact performed whole-sweep budgets preserve every fieldwise "
        "child,source and certificate decision");
  if (exact.base_pairs > 0) {
    --exact.base_pairs;
    const auto less =
        require(detail::boarding_lower_foot_transfer_surface_sweep_bounded(
            source, 0, 1, exact));
    accounting(less.surface, exact);
    check(!less.surface.complete && less.surface.first_refusal &&
              less.surface.first_refusal->condition ==
                  Condition::base_pair_capacity &&
              less.surface.examined_base_pairs == exact.base_pairs &&
              less.surface.completed_base_pairs == exact.base_pairs,
          "One-less observed base denominator refuses on correct last "
          "nextunattempted pair");
  }
}
auto request_controls(const OriginBoardingBootSupport& source) -> void {
  for (auto interval : std::array{std::array{1., 0.}, std::array{.25, .75},
                                  std::array{.75, .25}, std::array{0., 0.},
                                  std::array{.5, .5}, std::array{1., 1.}}) {
    const auto original = require(assess_origin_boarding_lower_foot_transfer(
        source, interval[0], interval[1]));
    const auto d =
        require(assess_origin_boarding_lower_foot_transfer_surface_sweep(
            source, interval[0], interval[1]));
    check(snapshot(d.transfer) == snapshot(original),
          "Full/reverse/sub/point sourcequery consumes exact original447 "
          "request and closedcells");
    inspect_union(d);
  }
}
auto lifetime_controls(const NativeCraftBinding& binding) -> void {
  auto d = [&] {
    const auto local = require(make_origin_boarding_boot_support(binding));
    Limits l;
    l.base_pairs = 0;
    return require(detail::boarding_lower_foot_transfer_surface_sweep_bounded(
        local, 0, 0, l));
  }();
  const auto saved = snapshot(d);
  auto copy = d;
  auto moved = std::move(d);
  check(snapshot(copy) == saved && snapshot(moved) == saved,
        "Moved/copied surface diagnosis retains ownedcell/source/name/refusal "
        "lifetime");
  const auto source =
      moved.transfer.initial.self.endpoint.sites.source.selected_partitions();
  check(source.size() == 10 &&
            moved.transfer.initial.self.endpoint.sites.source.contact() !=
                nullptr,
        "Shared immutable source remains alive after local provider and "
        "returned diagnostic moves");
  for (const auto& quad : source)
    for (const auto& face : quad.faces)
      check(!face.source_object.empty(),
            "Original source names remain owned through the447 child");
  if (moved.surface.first_refusal && moved.surface.first_refusal->key) {
    const auto& r = *moved.surface.first_refusal;
    check(!r.source_object.empty() && r.actual_triangle_metres.has_value(),
          "Actual stopping sourcekey retains borrowed name and copied bounded "
          "triangle context");
    const auto* contact =
        moved.transfer.initial.self.endpoint.sites.source.contact();
    bool found = false;
    const auto visit = require(detail::visit_effective_lower_cockpit_contact(
        *contact, [&](const auto& triangle) {
          if (triangle.key == *r.key) {
            found = true;
            check(triangle.source_object == r.source_object &&
                      triangle.object == r.object &&
                      triangle.evaluated_source_triangle ==
                          r.evaluated_source_triangle &&
                      triangle.obstacle &&
                      triangle.obstacle->points == *r.actual_triangle_metres,
                  "Refusal keys/names/triangles match original immutable "
                  "source across moves");
            return false;
          }
          return true;
        }));
    check(found &&
              visit.total_triangles == moved.surface.effective_triangle_count,
          "Original sourcecontext lookup retains full mathematical roster "
          "despite stopping callback");
  }
  moved.surface.complete = !moved.surface.complete;
  check(snapshot(copy) == saved,
        "Mutating returned surface report cannot modify its owned sibling");
  Limits l;
  l.base_pairs = 0;
  const auto fresh =
      require(detail::boarding_lower_foot_transfer_surface_sweep_bounded(
          copy.transfer.initial.self.endpoint.sites.source, 0, 0, l));
  check(snapshot(fresh) == saved, "Copied writable report cannot alter "
                                  "immutable source or fresh query evidence");
}
} // namespace
int main() {
  try {
    static_assert(sizeof(Payload) <=
                  kBoardingLowerFootTransferSurfaceMaximumPayloadBytes);
    static_assert(std::uint64_t{15} * 524288 * 1024 >
                  std::numeric_limits<std::uint32_t>::max());
    const auto binding = require(make_native_starting_assembly_binding(
        NativeStartingAssemblySelection{}));
    const auto source = require(make_origin_boarding_boot_support(binding));
    invalid_controls(source);
    environment_controls(source);
    numeric_fixtures();
    child_cap_controls(source);
    const auto unchanged =
        snapshot(require(assess_origin_boarding_lower_foot_transfer(source)));
    const auto old_static = snapshot(require(
        assess_origin_boarding_source_endpoint_surface_checkpoint(source)));
    // No whole moving surface outcome is asserted before this registered log.
    const auto actual = observe(source);
    check(snapshot(actual.transfer) == unchanged,
          "New source sweep owns one unchanged447 diagnostic rather than "
          "another pose graph");
    inspect_union(actual);
    source_bridge_and_sole_controls(actual.transfer);
    production_caps(source, actual);
    request_controls(source);
    lifetime_controls(binding);
    check(snapshot(require(
              assess_origin_boarding_lower_foot_transfer(source))) == unchanged,
          "New surface adapters/caps/lifetimes preserve original447 outputs "
          "completely fieldwise");
    check(snapshot(
              require(assess_origin_boarding_source_endpoint_surface_checkpoint(
                  source))) == old_static,
          "New shared adapters leave old static source checkpoint bytes/fields "
          "and original kernel order unchanged");
  } catch (const std::exception& e) {
    ++failures;
    std::cerr << "EXCEPTION: " << e.what() << '\n';
  }
  std::cout << checks << " lower-foot source-surface sweep checks, " << failures
            << " failures\n";
  return failures == 0 ? 0 : 1;
}
