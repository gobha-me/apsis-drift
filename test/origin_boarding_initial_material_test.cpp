#include "apsis_drift/origin_boarding_initial_material.hpp"
#include "origin_boarding_initial_material_internal.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cfenv>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <span>
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
using PointBounds = BoardingPlantedLegPointBounds;
using Load = BoardingSourceEndpointLoadDiagnostic;
using Diagnostic = BoardingInitialMaterialDiagnostic;
using Payload = BoardingInitialMaterialPayload;
using Limits = detail::BoardingInitialMaterialLimits;
using Condition = BoardingInitialMaterialCondition;
using Solid = detail::BoardingSourceEndpointSurfaceCheckpointSolidBounds;
using Shape = detail::BoardingSourceEndpointSurfaceCheckpointShape;
using Triangle = std::array<RigidVector3, 3>;
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
  if (!r) throw std::runtime_error("Required material test API refused");
  return std::move(*r);
}
auto component(RigidVector3 p, std::size_t i) -> double {
  return i == 0 ? p.x : i == 1 ? p.y : p.z;
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
auto point(RigidVector3 p) -> PointBounds {
  return {p, p};
}
auto box(RigidVector3 center = {}, RigidVector3 half = {.125, .125, .125})
    -> Solid {
  return {point(center), point(center), half, 0, Shape::box};
}
void no_authority(const Diagnostic& d) {
  check(!d.dynamics_qualified && !d.strength_qualified &&
            !d.continuous_qualified && !d.sweep_qualified &&
            !d.route_qualified && !d.actor_qualified && !d.seat_qualified &&
            !d.save_qualified && !d.first_flight_qualified,
        "Initial exclusion grants no route, strength, actor, save or flight "
        "permission");
}
void numeric_denied(const detail::MaterialEnclosureMathEvidence& e) {
  check(!e.source_qualified && !e.material_qualified && !e.actor_qualified,
        "Caller arithmetic encloser fixture grants no source or material "
        "authority");
}
auto cube_planes(double extent = 1) -> std::array<detail::MaterialPlane, 6> {
  return {{{{1, 0, 0}, {extent, extent, true}},
           {{-1, 0, 0}, {extent, extent, true}},
           {{0, 1, 0}, {extent, extent, true}},
           {{0, -1, 0}, {extent, extent, true}},
           {{0, 0, 1}, {extent, extent, true}},
           {{0, 0, -1}, {extent, extent, true}}}};
}
void arithmetic_controls() {
  const auto planes = cube_planes();
  const auto body = box();
  for (double shift : {-2., 0., 2.}) {
    const auto e = require(detail::initial_material_envelope_math(
        box({shift, 0, 0}), 0, {{-1, -1, -1}, {1, 1, 1}}));
    numeric_denied(e);
    const long double lower = static_cast<long double>(shift) - .125L,
                      upper = static_cast<long double>(shift) + .125L;
    check(e.arithmetic_supported && e.certified == (upper < -1 || lower > 1),
          "Independent box corner oracle corroborates strict full-envelope "
          "separation");
  }
  for (double center : {1.125, 1.124, 1.126}) {
    const auto e = require(detail::initial_material_primitive_math(
        box({center, 0, 0}), 0, planes));
    numeric_denied(e);
    check(e.arithmetic_supported && e.certified == (center > 1.125),
          "Exact touching and just-inside filled primitive cannot gain "
          "exclusion");
  }
  const auto inside =
      require(detail::initial_material_primitive_math(body, 0, planes));
  check(inside.arithmetic_supported && !inside.certified,
        "Surface-clear reservation wholly inside filled primitive refuses "
        "exclusion");
  numeric_denied(inside);
  // The authentic original finite-triangle kernel checks every sheet of this
  // closed cavity independently. Its cavity differs from a filled convex hull.
  const std::array<RigidVector3, 8> corners{{{-1, -1, -1},
                                             {1, -1, -1},
                                             {1, 1, -1},
                                             {-1, 1, -1},
                                             {-1, -1, 1},
                                             {1, -1, 1},
                                             {1, 1, 1},
                                             {-1, 1, 1}}};
  const std::array<std::array<std::size_t, 4>, 6> faces{{{0, 3, 2, 1},
                                                         {4, 5, 6, 7},
                                                         {0, 1, 5, 4},
                                                         {3, 7, 6, 2},
                                                         {0, 4, 7, 3},
                                                         {1, 2, 6, 5}}};
  for (const auto& f : faces)
    for (const auto& ids : std::array<std::array<std::size_t, 3>, 2>{
             {{f[0], f[1], f[2]}, {f[0], f[2], f[3]}}}) {
      const Triangle triangle{corners[ids[0]], corners[ids[1]],
                              corners[ids[2]]};
      const auto e =
          require(detail::boarding_source_endpoint_surface_checkpoint_pair_math(
              body, 0, triangle));
      check(e.arithmetic_supported && e.certified && !e.surface_qualified &&
                !e.world_qualified && !e.actor_qualified,
            "Original finite shell-sheet kernel preserves empty cavity without "
            "fake permission");
    }
  const auto first = point({0, -.5, 0}), last = point({0, .5, 0});
  for (double x : {0., .374, .375, .376, 1.}) {
    const auto e = require(detail::initial_material_capsule_math(
        box({x, 0, 0}), 0, first, last, .25));
    numeric_denied(e);
    check(e.arithmetic_supported && e.certified == (x > .375),
          "Independent straight capsule support oracle keeps touching/inside "
          "conservative");
  }
  const auto moved = require(
      detail::initial_material_primitive_math(box({2, -2, 0}), 2, planes));
  check(
      moved.certified,
      "Common world translation applied exactly once in enclosure arithmetic");
  const auto capsule_base = require(detail::initial_material_capsule_math(
      box({1, 0, 0}), 0, first, last, .25));
  check(capsule_base.certified && capsule_base.axes_examined > 0,
        "Finite separating capsule control reaches actual axes");
  const auto capsule_exact = require(detail::initial_material_capsule_math(
      box({1, 0, 0}), 0, first, last, .25,
      static_cast<std::size_t>(capsule_base.axes_examined)));
  check(capsule_exact.certified &&
            capsule_exact.axes_examined == capsule_base.axes_examined,
        "Exact actual capsule axis budget retains certificate");
  const auto capsule_less = require(detail::initial_material_capsule_math(
      box({1, 0, 0}), 0, first, last, .25,
      static_cast<std::size_t>(capsule_base.axes_examined - 1)));
  check(!capsule_less.certified &&
            capsule_less.axes_examined + 1 == capsule_base.axes_examined,
        "One-less actual capsule axes refuses without authority");
  const auto primitive_base = require(
      detail::initial_material_primitive_math(box({0, -2, 0}), 0, planes));
  check(primitive_base.certified && primitive_base.axes_examined > 0,
        "Finite separating primitive control reaches actual nonfirst plane");
  const auto primitive_exact = require(detail::initial_material_primitive_math(
      box({0, -2, 0}), 0, planes,
      static_cast<std::size_t>(primitive_base.axes_examined)));
  const auto primitive_less = require(detail::initial_material_primitive_math(
      box({0, -2, 0}), 0, planes,
      static_cast<std::size_t>(primitive_base.axes_examined - 1)));
  check(primitive_exact.certified && !primitive_less.certified,
        "Primitive exact and one-less actual axis budgets are meaningful");
  check(
      !require(
           detail::initial_material_capsule_math(body, 0, first, last, .25, 0))
              .certified &&
          !require(detail::initial_material_primitive_math(body, 0, planes, 0))
               .certified,
      "Zero arithmetic axis budgets never certify");
  const auto inf = std::numeric_limits<double>::infinity(),
             nan = std::numeric_limits<double>::quiet_NaN();
  for (double x : {nan, inf, -inf}) {
    auto b = body;
    b.first.lower.x = x;
    check(!detail::initial_material_envelope_math(b, 0,
                                                  {{-1, -1, -1}, {1, 1, 1}}),
          "Nonfinite full reservation cannot certify envelope");
    check(!detail::initial_material_capsule_math(body, 0, point({x, 0, 0}),
                                                 last, .25),
          "Nonfinite source capsule endpoint refuses");
    check(!detail::initial_material_capsule_math(body, 0, first, last, x),
          "Nonfinite radius refuses");
    auto p = planes;
    p[0].normal.x = x;
    check(!detail::initial_material_primitive_math(body, 0, p),
          "Nonfinite primitive normal refuses");
    p = planes;
    p[0].maximum.upper = x;
    check(!detail::initial_material_primitive_math(body, 0, p),
          "Nonfinite primitive offset refuses");
  }
  auto invalid = body;
  invalid.half_size_metres.x = -.1;
  check(!detail::initial_material_envelope_math(invalid, 0,
                                                {{-1, -1, -1}, {1, 1, 1}}),
        "Negative full box dimensions refuse");
  invalid = body;
  invalid.first.lower.x = 1;
  invalid.first.upper.x = 0;
  check(!detail::initial_material_envelope_math(invalid, 0,
                                                {{-1, -1, -1}, {1, 1, 1}}),
        "Reversed body bounds refuse");
  check(
      !detail::initial_material_capsule_math(body, 0, first, last, 0) &&
          !detail::initial_material_capsule_math(body, 0, first, last, .25, 17),
      "Radius and original axis ceiling remain bounded");
  check(!detail::initial_material_primitive_math(body, 0, {}) &&
            !detail::initial_material_primitive_math(body, 0, planes, 17),
        "Empty primitive and increased axis capacity refuse");
}
void signed_projection_regressions() {
  const auto planes = cube_planes();
  for (double sign : {-1., 1.}) {
    // This off-center box spans [.7,1.1] (or its mirror): its maximum
    // crossing a hull plane is not a separating minimum certificate.
    const auto overlap = box({sign * .9, 0, 0}, {.2, .125, .125});
    const auto primitive =
        require(detail::initial_material_primitive_math(overlap, 0, planes));
    check(primitive.arithmetic_supported && !primitive.certified,
          "Off-center box overlapping filled prism cannot substitute positive "
          "support for minimum projection");
    numeric_denied(primitive);
    const auto touching = box({sign * 1.25, 0, 0}, {.25, .125, .125});
    check(!require(detail::initial_material_primitive_math(touching, 0, planes))
               .certified,
          "Off-center full reservation exactly touching filled prism remains "
          "strict");
    const Solid capsule{point({sign * .25, -.5, 0}),
                        point({sign * .75, .5, 0}),
                        {},
                        .15,
                        Shape::capsule};
    check(!require(detail::initial_material_primitive_math(capsule, 0, planes))
               .certified,
          "Angled off-center capsule inside filled prism cannot gain "
          "minimum-from-maximum exemption");
    const auto source_first = point({sign * 1., -.5, 0}),
               source_last = point({sign * 1., .5, 0});
    const auto capsule_pair = require(detail::initial_material_capsule_math(
        capsule, 0, source_first, source_last, .2));
    // World point (+/-.875,.5,0) has distance .125 to both endpoint axes,
    // strictly below the original .15 and .2 radii. It witnesses overlap.
    check(.125 < .15 && .125 < .2 && !capsule_pair.certified &&
              capsule_pair.arithmetic_supported,
          "Asymmetric capsule enclosers with genuine common interior cannot "
          "separate using maxima");
    numeric_denied(capsule_pair);
    const auto body = box({sign * 1.5, 0, 0}, {.4, .125, .125});
    const auto box_pair = require(detail::initial_material_capsule_math(
        body, 0, source_first, source_last, .2));
    // (+/-1.15,0,0) is strictly inside both this full box and the source
    // cylinder, regardless of their differing positive-axis maxima.
    check(!box_pair.certified && box_pair.arithmetic_supported,
          "Asymmetric full box and capsule with common interior retain both "
          "signed support bounds");
    numeric_denied(box_pair);
  }
}
void source_summary(const OriginBoardingInitialMaterial& s) {
  const auto* p = s.summary();
  check(p && p->bindings_complete && p->constructors_complete,
        "Genuine source binds complete constructor and roster certificates");
  if (!p) return;
  check(p->original_objects == 1746 && p->effective_objects == 1751 &&
            p->fixed_objects == 1581 && p->moving_objects == 165 &&
            p->removed_objects == 8 && p->replacement_objects == 13 &&
            p->absent_crop_objects == 831,
        "All original, moving, removed, replacement and absent-crop objects "
        "retained");
  check(p->ring_inclusions == 1680 && p->support_plane_inclusions == 2304 &&
            p->source_bytes <= kBoardingInitialMaterialMaximumSourceBytes,
        "Actual original constructor work and owned source memory bounded");
  check(!s.source_name(0).empty() && s.source_name(1759).empty() &&
            s.source_name(std::numeric_limits<std::size_t>::max()).empty(),
        "Source name range rejects past-end and overflow indices");
}
void invalid_source_controls(const NativeCraftBinding& binding,
                             const OriginBoardingBootSupport& boot,
                             const OriginBoardingInitialMaterial& source) {
  check(!make_origin_boarding_initial_material(NativeCraftBinding{}),
        "Legacy unbound craft cannot authenticate material");
  auto selection = NativeStartingAssemblySelection{};
  selection.hardware.seat_boarding = 0;
  const auto wrong = make_native_starting_assembly_binding(selection);
  check(!wrong || !make_origin_boarding_initial_material(*wrong),
        "Wrong hardware cannot acquire canonical initial material capability");
  auto limits = Limits{};
  ++limits.source_bytes;
  check(!detail::BoardingInitialMaterialAccess::make(binding, limits) &&
            !detail::boarding_initial_material_bounded(boot, source, limits),
        "Private limit controls cannot raise original source ceiling");
  for (int i = 0; i < 3; ++i) {
    limits = {};
    if (i == 0) limits.source_bytes = 0;
    if (i == 1) limits.ring_inclusions = 0;
    if (i == 2) limits.support_plane_inclusions = 0;
    check(!detail::BoardingInitialMaterialAccess::make(binding, limits),
          "Reached zero source or constructor work cap prevents package "
          "admission");
  }
  source_summary(source);
  const auto summary = *source.summary();
  limits = {};
  limits.source_bytes = summary.source_bytes;
  limits.ring_inclusions = summary.ring_inclusions;
  limits.support_plane_inclusions = summary.support_plane_inclusions;
  source_summary(
      require(detail::BoardingInitialMaterialAccess::make(binding, limits)));
  for (int i = 0; i < 3; ++i) {
    auto l = limits;
    if (i == 0) --l.source_bytes;
    if (i == 1) --l.ring_inclusions;
    if (i == 2) --l.support_plane_inclusions;
    check(!detail::BoardingInitialMaterialAccess::make(binding, l),
          "One less actual constructor/source admission budget refuses");
  }
  for (int i = 0; i < 2; ++i) {
    limits = {};
    if (i == 0)
      limits.source_bytes = 0;
    else
      limits.envelope_pairs = 0;
    const auto d = require(
        detail::boarding_initial_material_bounded(boot, source, limits));
    check(!d.material.initial_material_exclusion && d.material.first_refusal &&
              d.material.first_refusal->condition ==
                  (i == 0 ? Condition::source_capacity
                          : Condition::envelope_capacity) &&
              d.material.envelope_pairs == 0,
          "Reached zero early query budget refuses before first source/body "
          "comparison");
    no_authority(d);
  }
  auto copy = source;
  auto moved = std::move(copy);
  check(!copy.summary() && copy.source_name(0).empty() &&
            !assess_origin_boarding_initial_material(boot, copy),
        "Moved-from material source refuses and loses views");
  source_summary(moved);
  auto boot_copy = boot;
  auto boot_moved = std::move(boot_copy);
  check(!assess_origin_boarding_initial_material(boot_copy, source),
        "Moved-from genuine boot support refuses original child");
  (void)boot_moved;
}
void unsafe_environment(const NativeCraftBinding& binding,
                        const OriginBoardingBootSupport& boot,
                        const OriginBoardingInitialMaterial& source) {
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
  const auto planes = cube_planes();
  auto inspect = [&]() {
    check(
        !make_origin_boarding_initial_material(binding),
        "Unsafe floating environment cannot authenticate source constructors");
    const auto d = assess_origin_boarding_initial_material(boot, source);
    check(!d || (!d->material.arithmetic_supported &&
                 !d->material.initial_material_exclusion),
          "Unsafe query environment cannot retain material acceptance");
    const auto e = detail::initial_material_capsule_math(
        box(), 0, point({0, -.5, 0}), point({0, .5, 0}), .25);
    check(!e || (!e->arithmetic_supported && !e->certified),
          "Unsafe numeric capsule retains no positive flags");
    const auto p = detail::initial_material_primitive_math(box(), 0, planes);
    check(!p || (!p->arithmetic_supported && !p->certified),
          "Unsafe numeric primitive retains no positive flags");
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
auto snapshot(const Payload& p) -> Snapshot {
  Snapshot s;
  s.integer(p.material_version);
  const auto& r = p.source;
  for (auto n :
       std::array{r.original_objects, r.effective_objects, r.fixed_objects,
                  r.moving_objects, r.removed_objects, r.replacement_objects,
                  r.absent_crop_objects, r.source_bytes})
    s.integer(n);
  s.integer(r.ring_inclusions);
  s.integer(r.support_plane_inclusions);
  s.integer(r.bindings_complete);
  s.integer(r.constructors_complete);
  for (const auto& part : p.parts) {
    s.integer(part.id);
    s.point(part.witness);
    s.point(part.full_reservation);
    s.integer(part.envelopes_examined);
    s.integer(part.envelope_exclusions);
    s.integer(part.relation_exclusions);
    s.integer(part.original_witness_identity);
    s.integer(part.complete);
  }
  for (auto n :
       std::array{p.envelope_pairs, p.enclosure_pairs, p.triangle_visits,
                  p.collapsed_triangles, p.boundary_pairs, p.axes_examined})
    s.integer(n);
  for (bool b :
       std::array{p.bindings_complete, p.source_complete,
                  p.arithmetic_supported, p.initial_material_exclusion})
    s.integer(b);
  s.integer(p.first_refusal.has_value());
  if (p.first_refusal) {
    const auto& f = *p.first_refusal;
    s.integer(f.condition);
    s.integer(f.part.has_value());
    if (f.part) s.integer(*f.part);
    s.integer(f.source.has_value());
    if (f.source) s.integer(*f.source);
    s.integer(f.triangle.has_value());
    if (f.triangle) s.integer(*f.triangle);
    s.names.emplace_back(f.source_object);
  }
  return s;
}
void accounting(const Diagnostic& d, const Limits& limits = {}) {
  no_authority(d);
  source_summary(d.source);
  const auto& p = d.material;
  check(p.material_version == kBoardingInitialMaterialVersion &&
            p.envelope_pairs <= limits.envelope_pairs &&
            p.enclosure_pairs <= limits.enclosure_pairs &&
            p.triangle_visits <= limits.triangle_visits &&
            p.boundary_pairs <= limits.boundary_pairs &&
            p.axes_examined <= limits.axes,
        "All actual bounded material stage work remains under selected limits");
  check(p.collapsed_triangles <= p.triangle_visits &&
            p.boundary_pairs <=
                15 * (p.triangle_visits - p.collapsed_triangles) &&
            p.axes_examined <=
                limits.pair_axes * (p.boundary_pairs + p.enclosure_pairs),
        "Collapsed faces do no boundary work and actual axes account for every "
        "pair");
  std::size_t pairs{};
  for (const auto& part : p.parts) {
    pairs += part.envelopes_examined;
    check(part.envelope_exclusions + part.relation_exclusions <=
              part.envelopes_examined,
          "Actual per-part exclusions cannot exceed examined envelopes");
  }
  check(pairs == p.envelope_pairs,
        "Per-part and aggregate actual envelope work agree");
  if (p.initial_material_exclusion) {
    check(!p.first_refusal && p.bindings_complete && p.source_complete &&
              p.arithmetic_supported && d.initial.load.complete &&
              d.initial.self.complete && d.initial.self.endpoint.complete &&
              p.envelope_pairs == 26265,
          "Initial material acceptance requires complete original child and "
          "entire effective roster");
    for (std::size_t i = 0; i < 15; ++i) {
      const auto& r = p.parts[i];
      check(static_cast<std::size_t>(r.id) == i &&
                r.original_witness_identity && r.complete &&
                r.envelopes_examined == 1751 &&
                r.envelope_exclusions + r.relation_exclusions == 1751,
            "Each unchanged full reservation excludes every complete current "
            "source object");
    }
  } else
    check(p.first_refusal.has_value(),
          "Every incomplete material result retains named refusal");
  if (p.first_refusal) {
    const auto& f = *p.first_refusal;
    if (f.part)
      check(*f.part < 15,
            "Refused body identity remains within original fifteen");
    if (f.source)
      check(*f.source < 1759 &&
                f.source_object == d.source.source_name(*f.source),
            "Retained first refusal owns exact source name/index");
  }
}
void owned_body(const Diagnostic& d) {
  const auto& e = d.initial.self.endpoint;
  if (!e.body || !d.material.bindings_complete) return;
  check(e.common_translation_y_metres == .847,
        "Original common translation is retained once");
  for (std::size_t i = 0; i < 15; ++i) {
    const auto& r = d.material.parts[i];
    check(static_cast<std::size_t>(r.id) == i && r.original_witness_identity,
          "Actual center/midpoint identity remains original full body");
    const auto& m = e.body->mass_points[i];
    for (std::size_t axis = 0; axis < 3; ++axis) {
      const auto low = static_cast<long double>(component(m.lower, axis)) +
                       (axis == 1 ? .847L : 0.L),
                 high = static_cast<long double>(component(m.upper, axis)) +
                        (axis == 1 ? .847L : 0.L);
      check(component(r.witness.lower, axis) <= low + 1e-15L &&
                component(r.witness.upper, axis) >= high - 1e-15L,
            "Independent original witness plus one world translation enclosed");
    }
    if (const auto* b = std::get_if<BoardingPlantedBodyBoxBinding>(
            &e.parts[i].reservation)) {
      const auto& c = e.body->points[static_cast<std::size_t>(b->center)];
      for (std::size_t axis = 0; axis < 3; ++axis) {
        const long double translation = axis == 1 ? .847L : 0.L;
        const auto low = static_cast<long double>(component(c.lower, axis)) -
                         component(b->half_size_metres, axis) + translation,
                   high = static_cast<long double>(component(c.upper, axis)) +
                          component(b->half_size_metres, axis) + translation;
        check(component(r.full_reservation.lower, axis) <= low + 1e-15L &&
                  component(r.full_reservation.upper, axis) >= high - 1e-15L,
              "Full WORLD box contains actual all-corner support, not SELF "
              "ellipsoid");
      }
      if (i == 5 || i == 11)
        check(r.full_reservation.lower.y == e.legs[i == 5 ? 0 : 1].plane_metres,
              "Only genuine unchanged initial boot retains strict sole-plane "
              "equality");
    } else {
      const auto& c =
          std::get<BoardingPlantedBodyCapsuleBinding>(e.parts[i].reservation);
      const auto &a = e.body->points[static_cast<std::size_t>(c.start)],
                 &last = e.body->points[static_cast<std::size_t>(c.end)];
      for (std::size_t axis = 0; axis < 3; ++axis) {
        const long double translation = axis == 1 ? .847L : 0.L;
        const auto low = static_cast<long double>(
                             std::min(component(a.lower, axis),
                                      component(last.lower, axis))) -
                         c.radius_metres + translation,
                   high = static_cast<long double>(
                              std::max(component(a.upper, axis),
                                       component(last.upper, axis))) +
                          c.radius_metres + translation;
        check(component(r.full_reservation.lower, axis) <= low + 1e-15L &&
                  component(r.full_reservation.upper, axis) >= high - 1e-15L,
              "Full original capsule endpoint-radius envelope corroborated "
              "independently");
      }
    }
  }
}
void observe_and_controls(const OriginBoardingBootSupport& boot,
                          const OriginBoardingInitialMaterial& source) {
  const auto old =
      snapshot(require(assess_origin_boarding_source_endpoint_load(boot)));
  const auto d = require(assess_origin_boarding_initial_material(boot, source));
  const auto& p = d.material;
  // First registered public observation precedes all outcome-dependent checks.
  std::cout << "FIRST_PUBLIC_MATERIAL exclusion="
            << p.initial_material_exclusion << " work=" << p.envelope_pairs
            << ',' << p.enclosure_pairs << ',' << p.triangle_visits << ','
            << p.collapsed_triangles << ',' << p.boundary_pairs << ','
            << p.axes_examined;
  if (p.first_refusal) {
    std::cout << " refusal="
              << static_cast<unsigned>(p.first_refusal->condition) << " part=";
    if (p.first_refusal->part) std::cout << *p.first_refusal->part;
    std::cout << " source=";
    if (p.first_refusal->source) std::cout << *p.first_refusal->source;
    std::cout << " name=" << p.first_refusal->source_object;
  }
  std::cout << '\n';
  accounting(d);
  owned_body(d);
  check(snapshot(d.initial) == old,
        "Material consumer retains fieldwise identical original owned "
        "load/self/endpoint child");
  Limits exact;
  exact.source_bytes = p.source.source_bytes;
  exact.ring_inclusions = p.source.ring_inclusions;
  exact.support_plane_inclusions = p.source.support_plane_inclusions;
  exact.envelope_pairs = p.envelope_pairs;
  exact.enclosure_pairs = p.enclosure_pairs;
  exact.triangle_visits = p.triangle_visits;
  exact.boundary_pairs = p.boundary_pairs;
  exact.axes = p.axes_examined;
  const auto replay =
      require(detail::boarding_initial_material_bounded(boot, source, exact));
  accounting(replay, exact);
  check(snapshot(replay.material) == snapshot(p) &&
            snapshot(replay.initial) == old,
        "Exact actual work budgets reproduce first complete or refused "
        "observation fieldwise");
  for (int stage = 0; stage < 5; ++stage) {
    auto l = exact;
    std::uint64_t* counter{};
    Condition condition{};
    switch (stage) {
      case 0:
        counter = &l.envelope_pairs;
        condition = Condition::envelope_capacity;
        break;
      case 1:
        counter = &l.enclosure_pairs;
        condition = Condition::pair_capacity;
        break;
      case 2:
        counter = &l.triangle_visits;
        condition = Condition::triangle_capacity;
        break;
      case 3:
        counter = &l.boundary_pairs;
        condition = Condition::pair_capacity;
        break;
      default:
        counter = &l.axes;
        condition = Condition::axis_capacity;
        break;
    }
    if (*counter == 0) continue;
    --*counter;
    const auto lesser =
        require(detail::boarding_initial_material_bounded(boot, source, l));
    accounting(lesser, l);
    check(!lesser.material.initial_material_exclusion &&
              lesser.material.first_refusal &&
              lesser.material.first_refusal->condition == condition,
          "One-less actually used stage work reaches capacity rather than "
          "hidden unrelated refusal");
  }
  for (int stage = 0; stage < 3; ++stage) {
    auto l = Limits{};
    if (stage == 0) l.enclosure_pairs = 0;
    if (stage == 1) l.triangle_visits = 0;
    if (stage == 2) l.axes = 0;
    const auto zero =
        require(detail::boarding_initial_material_bounded(boot, source, l));
    accounting(zero, l);
    const bool reached = stage == 0   ? p.enclosure_pairs > 0
                         : stage == 1 ? p.triangle_visits > 0
                                      : p.axes_examined > 0;
    if (reached)
      check(!zero.material.initial_material_exclusion &&
                zero.material.first_refusal &&
                zero.material.first_refusal->condition ==
                    (stage == 0   ? Condition::pair_capacity
                     : stage == 1 ? Condition::triangle_capacity
                                  : Condition::axis_capacity),
            "Observed genuinely reached stage has meaningful zero-budget "
            "refusal");
  }
  auto changed = d;
  changed.material.parts[0].full_reservation.lower.x = -999;
  changed.initial.load.complete = false;
  check(snapshot(require(assess_origin_boarding_source_endpoint_load(boot))) ==
            old,
        "Mutable returned report does not alter immutable original child "
        "producer");
  auto copy = source;
  auto moved = std::move(copy);
  const auto owned =
      require(assess_origin_boarding_initial_material(boot, moved));
  const auto name = owned.source.source_name(0);
  moved = source;
  check(!name.empty() && name == owned.source.source_name(0) &&
            snapshot(owned.material) == snapshot(p),
        "Report-retained source names and immutable arrays survive handle move "
        "and reassignment");
}
using Wide = std::array<long double, 3>;
auto wide(RigidVector3 p) -> Wide {
  return {p.x, p.y, p.z};
}
auto minus(Wide a, Wide b) -> Wide {
  for (std::size_t i = 0; i < 3; ++i)
    a[i] -= b[i];
  return a;
}
auto dot(Wide a, Wide b) -> long double {
  long double out{};
  for (std::size_t i = 0; i < 3; ++i)
    out += a[i] * b[i];
  return out;
}
auto cross(Wide a, Wide b) -> Wide {
  return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2],
          a[0] * b[1] - a[1] * b[0]};
}
auto transform(const OperatingTransform& m, RigidVector3 p) -> Wide {
  Wide out{};
  for (std::size_t row = 0; row < 3; ++row) {
    out[row] = component(m.columns[3], row);
    for (std::size_t col = 0; col < 3; ++col)
      out[row] += static_cast<long double>(component(m.columns[col], row)) *
                  component(p, col);
  }
  return out;
}
auto quantized(const detail::MaterialQuantizedPoint& q) -> Wide {
  return {q.value[0] * 1e-6L, q.value[1] * 1e-6L, q.value[2] * 1e-6L};
}
void constructor_denied(const detail::MaterialConstructorMathEvidence& e) {
  check(!e.source_qualified && !e.material_qualified && !e.actor_qualified,
        "Constructor arithmetic fixture cannot authenticate caller source");
}
void constructor_controls() {
  const auto view = detail::origin_boarding_initial_material_prepared();
  check(view.sources.size() == 1759 && view.meshes.size() == 19 &&
            view.curves.size() == 3 && view.supports.size() == 2 &&
            view.moving.size() == 165,
        "Immutable complete source fixture has bounded exact packet roster");
  std::uint64_t ring_work{}, support_work{};
  bool constructors_complete = true;
  for (const auto& c : view.curves) {
    const auto& s = view.sources[c.source];
    const auto& m = view.meshes[c.mesh];
    const auto outcome =
        detail::initial_material_curve_constructor_math(s, c, m);
    std::cout << "FIRST_CURVE_CONSTRUCTOR source=" << c.source
              << " certified=" << (outcome && outcome->certified);
    if (!outcome) std::cout << " error=" << outcome.error();
    std::cout << '\n';
    if (!outcome) {
      constructors_complete = false;
      continue;
    }
    const auto e = *outcome;
    constructor_denied(e);
    check(e.arithmetic_supported && e.certified &&
              e.inclusion_predicates == 6 * m.triangles.size(),
          "Actual curve validates both representations of every three-vertex "
          "ring face");
    ring_work += e.inclusion_predicates;
    const auto exact = require(detail::initial_material_curve_constructor_math(
        s, c, m, e.inclusion_predicates));
    check(
        exact.certified &&
            !detail::initial_material_curve_constructor_math(
                s, c, m, e.inclusion_predicates - 1) &&
            !detail::initial_material_curve_constructor_math(s, c, m, 0),
        "Actual curve exact/one-less/zero inclusion work is reached honestly");
    for (const auto& face : m.triangles) {
      const auto segment =
          *std::min_element(face.vertices.begin(), face.vertices.end()) / 10;
      const auto a = transform(c.corrected_world, c.local_points[segment]),
                 b = transform(c.corrected_world, c.local_points[segment + 1]),
                 d = minus(b, a);
      for (auto vertex : face.vertices)
        for (bool q : {false, true}) {
          const auto p = q ? quantized(m.quantized_vertices[vertex])
                           : wide(m.raw_vertices[vertex]);
          const auto r = minus(p, a);
          const auto t = std::clamp(dot(r, d) / dot(d, d), 0.L, 1.L);
          auto gap = r;
          for (std::size_t axis = 0; axis < 3; ++axis)
            gap[axis] -= t * d[axis];
          const auto radius = static_cast<long double>(c.radius_metres) + 1e-6L;
          check(dot(gap, gap) <= radius * radius + 1e-16L,
                "Independent projected segment distance contains each actual "
                "ring-strip vertex");
        }
    }
    auto bad = c;
    bad.local_points[0].x = std::nextafter(
        bad.local_points[0].x, std::numeric_limits<double>::infinity());
    check(!detail::initial_material_curve_constructor_math(s, bad, m),
          "Changed actual POLY point refuses original constructor identity");
    bad = c;
    bad.radius_metres = std::nextafter(c.radius_metres,
                                       std::numeric_limits<double>::infinity());
    check(!detail::initial_material_curve_constructor_math(s, bad, m),
          "Changed stored radius setting refuses");
    bad = c;
    bad.source = (c.source + 1) % view.sources.size();
    check(!detail::initial_material_curve_constructor_math(s, bad, m),
          "Changed curve owner refuses");
    bad = c;
    bad.corrected_world.columns[3].x += 1;
    check(!detail::initial_material_curve_constructor_math(s, bad, m),
          "Double or changed curve provenance transform refuses");
    auto source = s;
    source.raw_fingerprint = "bad-hash";
    check(!detail::initial_material_curve_constructor_math(source, c, m),
          "Changed source fingerprint refuses constructor authority");
    source = s;
    source.parent = "other-owner";
    check(!detail::initial_material_curve_constructor_math(source, c, m),
          "Changed actual parent refuses original constructor");
    auto mesh = m;
    std::vector<RigidVector3> raw(m.raw_vertices.begin(), m.raw_vertices.end());
    raw[0].x += 1;
    mesh.raw_vertices = raw;
    check(!detail::initial_material_curve_constructor_math(s, c, mesh),
          "Actual ring vertex outside fixed enclosure refuses shared "
          "containment proof");
    mesh = m;
    mesh.raw_vertices = mesh.raw_vertices.first(mesh.raw_vertices.size() - 1);
    check(!detail::initial_material_curve_constructor_math(s, c, mesh),
          "Truncated original ring packet refuses before buffer access");
    mesh = m;
    std::vector<detail::MaterialTriangle> triangles(m.triangles.begin(),
                                                    m.triangles.end());
    triangles[0].vertices[0] =
        static_cast<std::uint32_t>(m.raw_vertices.size());
    mesh.triangles = triangles;
    check(!detail::initial_material_curve_constructor_math(s, c, mesh),
          "Out-of-range ring-strip face refuses safely");
  }
  for (const auto& c : view.supports) {
    const auto& s = view.sources[c.source];
    const auto& m = view.meshes[c.mesh];
    const auto outcome =
        detail::initial_material_support_constructor_math(s, c, m);
    std::cout << "FIRST_SUPPORT_CONSTRUCTOR source=" << c.source
              << " certified=" << (outcome && outcome->certified);
    if (!outcome) std::cout << " error=" << outcome.error();
    std::cout << '\n';
    if (!outcome) {
      constructors_complete = false;
      continue;
    }
    const auto e = *outcome;
    constructor_denied(e);
    check(e.arithmetic_supported && e.certified &&
              e.inclusion_predicates == 12 * m.raw_vertices.size(),
          "Actual support validates every evaluated vertex against all six "
          "planes twice");
    support_work += e.inclusion_predicates;
    const auto exact =
        require(detail::initial_material_support_constructor_math(
            s, c, m, e.inclusion_predicates));
    check(exact.certified &&
              !detail::initial_material_support_constructor_math(
                  s, c, m, e.inclusion_predicates - 1) &&
              !detail::initial_material_support_constructor_math(s, c, m, 0),
          "Support exact/one-less/zero inclusion work preserves genuine "
          "capacity boundaries");
    std::array<Wide, 8> vertices;
    for (std::size_t i = 0; i < 8; ++i)
      vertices[i] = transform(c.corrected_world, c.local_vertices[i]);
    for (const auto& face : c.polygons) {
      const auto a = vertices[face[0]], n = cross(minus(vertices[face[1]], a),
                                                  minus(vertices[face[2]], a));
      long double maximum = -std::numeric_limits<long double>::infinity();
      for (const auto& p : vertices)
        maximum = std::max(maximum, dot(p, n));
      const auto allowance =
          1e-6L * (std::abs(n[0]) + std::abs(n[1]) + std::abs(n[2]));
      for (std::size_t i = 0; i < m.raw_vertices.size(); ++i)
        for (bool q : {false, true})
          check(dot(q ? quantized(m.quantized_vertices[i])
                      : wide(m.raw_vertices[i]),
                    n) <= maximum + allowance + 1e-16L,
                "Independent authentic tapered-prism planes contain actual "
                "evaluated bevel vertices");
    }
    auto bad = c;
    bad.bevel_width =
        std::nextafter(c.bevel_width, std::numeric_limits<double>::infinity());
    check(!detail::initial_material_support_constructor_math(s, bad, m),
          "Changed bevel setting refuses actual support identity");
    bad = c;
    bad.local_vertices[0].z += .01;
    check(!detail::initial_material_support_constructor_math(s, bad, m),
          "Changed premodifier tapered vertex refuses");
    bad = c;
    bad.source = (c.source + 1) % view.sources.size();
    check(!detail::initial_material_support_constructor_math(s, bad, m),
          "Changed support owner refuses");
    auto source = s;
    source.quantized_fingerprint = "bad-game-hash";
    check(!detail::initial_material_support_constructor_math(source, c, m),
          "Changed quantized source hash refuses");
    auto mesh = m;
    std::vector<RigidVector3> raw(m.raw_vertices.begin(), m.raw_vertices.end());
    raw[0].y += 1;
    mesh.raw_vertices = raw;
    check(!detail::initial_material_support_constructor_math(s, c, mesh),
          "Changed evaluated bevel point fails actual fixed-prism containment");
    mesh = m;
    mesh.source = (mesh.source + 1) % view.sources.size();
    check(!detail::initial_material_support_constructor_math(s, c, mesh),
          "Mismatched support packet owner refuses");
    bad = c;
    bad.polygons[0][0] = 8;
    check(!detail::initial_material_support_constructor_math(s, bad, m),
          "Out-of-range premodifier polygon refuses safely");
  }
  if (constructors_complete)
    check(ring_work == 1680 && support_work == 2304,
          "Independent full constructor work reconciles frozen finite counts");
}
void envelope_contains(const PointBounds& envelope, Wide value) {
  for (std::size_t axis = 0; axis < 3; ++axis) {
    // Only the independently evaluated long-double sum has roundoff here.
    // This check does not enlarge or alter the stored source certificate.
    const auto error = 16 * std::numeric_limits<long double>::epsilon() *
                       std::max(1.L, std::abs(value[axis]));
    check(static_cast<long double>(component(envelope.lower, axis)) <=
                  value[axis] + error &&
              static_cast<long double>(component(envelope.upper, axis)) >=
                  value[axis] - error,
          "Retained envelope contains independently transformed actual C++ "
          "corner");
  }
}
void source_roster(const NativeCraftBinding& binding,
                   const OriginBoardingInitialMaterial& source) {
  const auto view = detail::origin_boarding_initial_material_prepared();
  std::array<bool, 1759> moving_seen{};
  std::size_t removed{}, replacement{}, absent{};
  for (std::size_t i = 0; i < view.sources.size(); ++i) {
    const auto& r = view.sources[i];
    check(source.source_name(i) == r.name,
          "Retained source names keep exact original namespaces");
    removed += r.removed;
    replacement += r.replacement;
    absent += i < 1746 && r.absent_crop;
    check((i >= 1746) == r.replacement,
          "Replacement roster never aliases original namespace");
  }
  check(
      removed == 8 && replacement == 13 && absent == 831,
      "Exact removals/replacements and absent-crop envelopes remain included");
  for (const auto& m : view.moving) {
    check(m.source < 1746 && !moving_seen[m.source],
          "Every moving original has exactly one complete summary");
    if (m.source >= 1746) continue;
    moving_seen[m.source] = true;
    const auto& r = view.sources[m.source];
    check(r.motion_group >= 0 && r.motion_group < 13,
          "Moving summary binds original group");
    if (r.motion_group < 0 || r.motion_group >= 13) continue;
    const auto envelope =
        detail::initial_material_source_envelope(source, m.source);
    if (r.removed) {
      check(!envelope,
            "Exactly removed original row cannot re-enter effective envelopes");
      continue;
    }
    check(envelope.has_value(),
          "Genuine retained moving source exposes diagnostic envelope");
    if (!envelope) continue;
    const auto& matrix =
        binding.pose()
            ->craft_world_deltas[static_cast<std::size_t>(r.motion_group)];
    for (const auto& rest : std::array{r.raw_bounds, r.quantized_bounds})
      for (unsigned mask = 0; mask < 8; ++mask) {
        const RigidVector3 corner{mask & 1 ? rest.upper.x : rest.lower.x,
                                  mask & 2 ? rest.upper.y : rest.lower.y,
                                  mask & 4 ? rest.upper.z : rest.lower.z};
        envelope_contains(*envelope, transform(matrix, corner));
      }
    // Captured posed arrays are a separate bound retained in the UNION policy.
    // Their authoring matrices are not equated with the actual C++ matrices.
    for (const auto& posed : std::array{m.raw_bounds, m.quantized_bounds})
      for (std::size_t axis = 0; axis < 3; ++axis)
        check(component(envelope->lower, axis) <=
                      component(posed.lower, axis) &&
                  component(envelope->upper, axis) >=
                      component(posed.upper, axis),
              "Effective union includes complete captured posed raw and game "
              "bounds");
  }
  check(!detail::initial_material_source_envelope(source, 1759) &&
            !detail::initial_material_source_envelope(
                source, std::numeric_limits<std::size_t>::max()),
        "Diagnostic envelope getter safely rejects past-end source indices");
  auto copy = source;
  auto retained = std::move(copy);
  check(!detail::initial_material_source_envelope(copy, 0) &&
            detail::initial_material_source_envelope(retained, 0).has_value(),
        "Moved source loses diagnostic envelope while retained handle "
        "preserves it");
}
} // namespace
int main() {
  try {
    static_assert(sizeof(Payload) <= 16384);
    arithmetic_controls();
    signed_projection_regressions();
    constructor_controls();
    const auto binding = require(make_native_starting_assembly_binding(
        NativeStartingAssemblySelection{}));
    const auto boot = require(make_origin_boarding_boot_support(binding));
    const auto admission = make_origin_boarding_initial_material(binding);
    std::cout << "FIRST_GENUINE_MATERIAL_FACTORY accepted="
              << admission.has_value();
    if (!admission) std::cout << " error=" << admission.error();
    std::cout << '\n';
    // Genuine source constructor admission is itself an unknown first outcome.
    // A refusal remains an honest receipt; no public material query can follow.
    if (admission) {
      const auto source = *admission;
      invalid_source_controls(binding, boot, source);
      unsafe_environment(binding, boot, source);
      source_roster(binding, source);
      observe_and_controls(boot, source);
    }
  } catch (const std::exception& e) {
    ++failures;
    std::cerr << "EXCEPTION: " << e.what() << '\n';
  }
  std::cout << checks << " checks, " << failures << " failures\n";
  return failures == 0 ? 0 : 1;
}
