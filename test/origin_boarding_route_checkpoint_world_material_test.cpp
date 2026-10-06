#include "apsis_drift/origin_boarding_route_checkpoint_world_material.hpp"
#include "origin_boarding_lower_foot_transfer_surface_sweep_internal.hpp"
#include "origin_boarding_route_checkpoint_world_material_internal.hpp"
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
using PointBounds = BoardingPlantedLegPointBounds;
using Solid = detail::BoardingSourceEndpointSurfaceCheckpointSolidBounds;
using Shape = detail::BoardingSourceEndpointSurfaceCheckpointShape;
using Triangle = std::array<RigidVector3, 3>;
using Diagnostic = BoardingRouteCheckpointWorldMaterialDiagnostic;
using Child = BoardingRouteCheckpointUnloadSelf02Diagnostic;
using Limits = detail::BoardingRouteCheckpointWorldLimits;
using Condition = BoardingRouteCheckpointWorldCondition;
using Proxy = detail::BoardingRouteCheckpointWorldProxy;
using Access = detail::BoardingRouteCheckpointWorldMaterialAccess;
using Wide = std::array<long double, 3>;
std::size_t checks{};
int failures{};
void check(bool pass, std::string_view why) {
  ++checks;
  if (!pass) {
    ++failures;
    std::cerr << "FAIL: " << why << '\n';
  }
}
template <class T, class E> auto require(std::expected<T, E> r) -> T {
  if (!r)
    throw std::runtime_error("Required WORLD/material API refused: " +
                             std::string(r.error()));
  return std::move(*r);
}
auto component(RigidVector3 p, std::size_t i) -> double {
  return i == 0 ? p.x : i == 1 ? p.y : p.z;
}
auto contains(double low, double high, long double v) -> bool {
  const auto corroboration = 2e-12L * std::max(1.L, std::abs(v));
  return std::isfinite(low) && std::isfinite(high) && low <= high &&
         low <= v + corroboration && high >= v - corroboration;
}
struct Snapshot {
  std::vector<std::uint64_t> values;
  std::vector<std::string> names;
  friend auto operator==(const Snapshot&, const Snapshot&) -> bool = default;
  template <class T> void integer(T v) {
    values.push_back(static_cast<std::uint64_t>(v));
  }
  void number(double v) { values.push_back(std::bit_cast<std::uint64_t>(v)); }
  template <class B> void scalar(B b) {
    number(b.lower);
    number(b.upper);
  }
  void point(RigidVector3 p) {
    number(p.x);
    number(p.y);
    number(p.z);
  }
  void point(PointBounds p) {
    point(p.lower);
    point(p.upper);
  }
  void jet(const BoardingPlantedBodyPointEvidence& p) {
    point(p.value);
    point(p.derivatives.velocity);
    point(p.derivatives.acceleration);
  }
};
auto snapshot(const Child& d) -> Snapshot {
  Snapshot s;
  s.integer(d.self_policy_version);
  s.integer(d.unload_version);
  for (double n :
       {d.requested_first, d.requested_last, d.reporting_elapsed_seconds})
    s.number(n);
  for (bool b :
       {d.reverse, d.arithmetic_supported, d.kinematics_complete,
        d.timing_complete, d.self_complete, d.nonnegative_reactions,
        d.nominal_vertical_equilibrium_complete, d.finite_pressure_complete,
        d.support_complete, d.endpoint_zero_port_reaction, d.complete})
    s.integer(b);
  for (auto b : d.join_expression_identity)
    s.integer(b);
  for (auto b : d.qualified_joins)
    s.integer(b);
  for (auto n :
       {d.hip_complement_attempts, d.work.phase.graphs, d.work.phase.legs,
        d.work.phase.bodies, d.work.phase.sectors, d.work.phase.timing,
        d.work.contact.self_pairs, d.work.contact.proposed_axes,
        d.work.contact.signed_trials, d.work.ownership_attempts,
        d.work.contact.pressure_candidates, d.work.contact.disk_edges})
    s.integer(n);
  for (auto n : {d.examined_nodes, d.mandatory_splits, d.maximum_depth,
                 d.output_capacity_bytes, d.owned_initial_quad_guards,
                 d.owned_initial_quad_edges, d.owned_initial_convexity_signs,
                 d.owned_initial_triangle_windings,
                 d.owned_initial_diagonal_incidence_guards})
    s.integer(n);
  for (const auto& r : d.controls) {
    for (const auto& root : r.root)
      for (const auto& c : root.coordinates) {
        s.integer(c.count);
        for (auto t : c.terms)
          s.number(t);
      }
    for (auto t : r.root_yaw_half)
      s.number(t);
    for (auto t : r.torso_lean_half)
      s.number(t);
    for (auto t : r.port_reaction_fraction)
      s.number(t);
    s.number(r.seconds_per_parameter);
    for (const auto& f : r.feet) {
      for (const auto& p : f.sole)
        for (const auto& c : p.coordinates) {
          s.integer(c.count);
          for (auto t : c.terms)
            s.number(t);
        }
      for (auto t : f.yaw_half)
        s.number(t);
      s.number(f.swing_height_metres);
    }
  }
  for (const auto& p : d.parts) {
    s.integer(p.id);
    s.integer(p.reservation.index());
    s.integer(p.mass.first);
    s.integer(p.mass.second);
    s.integer(p.mass.weight);
    if (const auto* b =
            std::get_if<BoardingRoutePhaseBoxBinding>(&p.reservation)) {
      s.integer(b->center);
      s.integer(b->frame);
      s.point(b->half_size_metres);
    } else {
      const auto& c =
          std::get<BoardingPlantedBodyCapsuleBinding>(p.reservation);
      s.integer(c.start);
      s.integer(c.end);
      s.number(c.radius_metres);
    }
  }
  s.integer(d.initial != nullptr);
  if (d.initial) {
    const auto& i = *d.initial;
    const auto& e = i.self.endpoint;
    s.number(e.common_translation_y_metres);
    s.number(e.root_z_metres);
    s.integer(e.complete);
    s.integer(i.self.complete);
    s.integer(i.load.complete);
    if (e.body) {
      for (auto p : e.body->points)
        s.point(p);
      for (auto p : e.body->mass_points)
        s.point(p);
      s.point(e.body->center_of_mass);
    }
    for (const auto& p : i.self.pairs) {
      s.integer(p.certificate);
      s.scalar(p.certificate_gap);
      s.scalar(p.ownership_extent);
      s.scalar(p.ownership_limit);
      s.integer(p.certified);
      s.integer(p.examined_axes);
    }
    for (const auto& p : i.load.pressures) {
      s.number(p.plane_metres);
      for (auto b : p.pressure_bounds_metres)
        s.point(b);
      for (auto k : p.source_keys) {
        s.integer(k.buffer);
        s.integer(k.group);
        s.integer(k.triangle);
      }
      s.integer(p.complete);
    }
    for (const auto& q : i.load.source_quads) {
      s.scalar(q.minimum_edge_squared);
      s.scalar(q.minimum_signed_side);
      s.integer(q.checked_edges);
      s.integer(q.checked_side_signs);
      s.integer(q.valid);
    }
  }
  for (const auto& c : d.cells) {
    s.number(c.global_first);
    s.number(c.global_last);
    s.integer(c.phase_index);
    const auto& a = c.assessment;
    const auto& p = a.phase;
    s.number(p.first);
    s.number(p.last);
    for (const auto& v : p.points)
      s.jet(v);
    for (const auto& v : p.mass_points)
      s.jet(v);
    s.jet(p.center_of_mass);
    for (const auto& f : p.frames)
      for (const auto& v : f.columns)
        s.jet(v);
    for (const auto& l : p.legs) {
      for (auto b : std::array{l.distance_squared, l.rho_squared,
                               l.gamma_squared, l.alpha, l.gamma})
        s.scalar(b);
      for (auto j :
           std::array{l.hip_pitch, l.shin_pitch, l.knee_flex, l.hip_axial,
                      l.hip_abduction, l.ankle_pitch, l.ankle_roll}) {
        s.scalar(j.rate);
        s.scalar(j.coordinate_second);
      }
      for (auto b : l.joint_speeds)
        s.scalar(b);
      for (auto b : l.sector_margins)
        s.scalar(b);
      for (bool b : {l.nominal_links, l.target_sole_identity,
                     l.derivative_domains, l.joint_sectors, l.timing_complete})
        s.integer(b);
    }
    for (auto b :
         std::array{p.root_speed, p.root_acceleration, p.root_yaw_speed,
                    p.torso_joint_speed, p.port_reaction_fraction})
      s.scalar(b);
    for (auto b : p.sole_center_speed)
      s.scalar(b);
    for (auto b : p.sole_yaw_speed)
      s.scalar(b);
    for (auto b : p.whole_sole_speed)
      s.scalar(b);
    for (bool b :
         {p.arithmetic_supported, p.nominal_links, p.target_sole_identities,
          p.joint_sectors, p.derivative_domains, p.timing_complete, p.complete,
          a.arithmetic_supported, a.self_complete, a.nonnegative_reactions,
          a.nominal_vertical_equilibrium_complete, a.finite_pressure_complete,
          a.endpoint_zero_port_reaction, a.complete})
      s.integer(b);
    for (auto b : a.barycenter_xz)
      s.scalar(b);
    for (auto b : a.common_pressure_delta_xz)
      s.scalar(b);
    s.scalar(a.port_reaction_fraction);
    for (const auto& o : a.owners) {
      s.scalar(o.extent);
      s.scalar(o.limit);
      s.scalar(o.secondary_extent);
      s.integer(o.certificate);
      s.integer(o.arithmetic_supported);
      s.integer(o.certified);
      s.integer(o.structural_identity);
    }
    for (auto n : a.pair_certificates)
      s.integer(n);
    for (auto n : a.certificate_counts)
      s.integer(n);
    for (const auto& q : a.pressures) {
      for (auto b : q.pressure_xz)
        s.scalar(b);
      s.scalar(q.sole_minimum_signed_side);
      s.scalar(q.sole_minimum_squared_gap);
      s.integer(q.source_partition);
      s.integer(q.scanned_partitions);
      for (const auto& t : q.candidates) {
        s.scalar(t.minimum_signed_side);
        s.scalar(t.minimum_squared_gap);
        s.integer(t.status);
        s.integer(t.arithmetic_supported);
        s.integer(t.quad_valid);
      }
      for (bool b : {q.arithmetic_supported, q.coplanar, q.sole_disk_contained,
                     q.source_disk_contained, q.coverage_complete, q.complete})
        s.integer(b);
    }
    for (auto n : c.self02_certificates)
      s.integer(n);
    for (auto n : c.self02_certificate_counts)
      s.integer(n);
    for (const auto& h : c.hip_complements) {
      s.integer(h.side);
      s.integer(h.region);
      s.integer(h.pair);
      s.number(h.slab_limit_metres);
      for (auto b : h.axis)
        s.scalar(b);
      for (auto b : std::array{h.transverse, h.extent, h.limit, h.strict_gap})
        s.scalar(b);
      for (bool b : {h.attempted, h.arithmetic_supported,
                     h.nominal_unit_identity, h.upright_pelvis_identity,
                     h.original_slab_identity, h.extent_evaluated, h.certified})
        s.integer(b);
    }
  }
  s.integer(d.first_refusal.has_value());
  if (d.first_refusal) {
    const auto& r = *d.first_refusal;
    s.integer(r.condition);
    s.integer(r.predicate_condition);
    s.integer(r.self02_condition);
    s.number(r.first);
    s.number(r.last);
    s.integer(r.phase_index.has_value());
    if (r.phase_index) s.integer(*r.phase_index);
    s.integer(r.pair.has_value());
    if (r.pair) s.integer(*r.pair);
  }
  return s;
}
auto box(RigidVector3 center = {}, RigidVector3 half = {.1, .1, .1}) -> Solid {
  return {{center, center}, {center, center}, half, 0, Shape::box};
}
auto plane_x(double x, double low = -1, double high = 1) -> Triangle {
  return {RigidVector3{x, low, -1}, RigidVector3{x, high, -1},
          RigidVector3{x, 0, 1}};
}
auto frame_identity() -> std::array<PointBounds, 3> {
  return {PointBounds{{1, 0, 0}, {1, 0, 0}}, PointBounds{{0, 1, 0}, {0, 1, 0}},
          PointBounds{{0, 0, 1}, {0, 0, 1}}};
}
void raw_denied(const detail::BoardingRouteCheckpointWorldProxyMath& p) {
  check(!p.body_qualified && !p.self_qualified && !p.source_qualified &&
            !p.material_qualified && !p.world_qualified && !p.route_qualified &&
            !p.actor_qualified && !p.sole_qualified,
        "Raw frame enclosure cannot grant body, sole, source, material or "
        "actor authority");
}
void corner_oracle(PointBounds center,
                   const std::array<PointBounds, 3>& columns, RigidVector3 half,
                   const Solid& solid, PointBounds enclosure) {
  for (std::size_t i = 0; i < 3; ++i) {
    long double H = 0;
    for (std::size_t j = 0; j < 3; ++j)
      H += static_cast<long double>(component(half, j)) *
           std::max(std::abs(static_cast<long double>(
                        component(columns[j].lower, i))),
                    std::abs(static_cast<long double>(
                        component(columns[j].upper, i))));
    check(static_cast<long double>(component(solid.half_size_metres, i)) +
                  2e-12L >=
              H,
          "Axis proxy retains full rotated interval-corner extents");
    check(contains(component(enclosure.lower, i), component(enclosure.upper, i),
                   static_cast<long double>(component(center.lower, i)) - H) &&
              contains(
                  component(enclosure.lower, i), component(enclosure.upper, i),
                  static_cast<long double>(component(center.upper, i)) + H),
          "Independent complete rotated corner extrema enclosed without extra "
          "placement");
  }
  check(solid.shape == Shape::box && solid.first.lower == center.lower &&
            solid.first.upper == center.upper,
        "Full WORLD box proxy keeps actual already-placed center; no double "
        ".847");
}
void frame_controls() {
  const PointBounds center{{.3, .847, -.6}, {.3, .847, -.6}};
  auto columns = frame_identity();
  columns[0] = {{.6, 0, -.8}, {.6, 0, -.8}};
  columns[2] = {{.8, 0, .6}, {.8, 0, .6}};
  for (auto half :
       std::array{RigidVector3{.24, .12, .18}, RigidVector3{.26, .2695, .18},
                  RigidVector3{.16, .18, .18}, RigidVector3{.06, .05, .14},
                  RigidVector3{.04, .05, .02}}) {
    const auto p =
        require(detail::boarding_route_checkpoint_world_frame_box_math(
            center, columns, half));
    raw_denied(p);
    check(p.arithmetic_supported,
          "Finite frame expression supports original full-box dimensions");
    corner_oracle(center, columns, half, p.solid, p.bounds);
    for (unsigned corner = 0; corner < 8; ++corner) {
      Wide v{center.lower.x, center.lower.y, center.lower.z};
      for (std::size_t j = 0; j < 3; ++j)
        for (std::size_t i = 0; i < 3; ++i)
          v[i] += (corner & (1U << j) ? 1.L : -1.L) * component(half, j) *
                  static_cast<long double>(component(columns[j].lower, i));
      for (std::size_t i = 0; i < 3; ++i)
        check(contains(component(p.bounds.lower, i),
                       component(p.bounds.upper, i), v[i]),
              "Every independent original rotated box corner is retained");
    }
  }
  const auto stretched =
      require(detail::boarding_route_checkpoint_world_frame_box_math(
          center, columns, {.24, .12, .18}));
  check(
      stretched.solid.half_size_metres.x > .24 &&
          stretched.solid.half_size_metres.z > .18,
      "Real yaw protrusion rules out identity-half or SELF ellipsoid shortcut");
  columns[0].lower.x = .55;
  columns[0].upper.x = .65;
  columns[2].lower.z = .5;
  columns[2].upper.z = .7;
  const auto interval =
      require(detail::boarding_route_checkpoint_world_frame_box_math(
          center, columns, {.24, .12, .18}));
  corner_oracle(center, columns, {.24, .12, .18}, interval.solid,
                interval.bounds);
  const auto nan = std::numeric_limits<double>::quiet_NaN(),
             inf = std::numeric_limits<double>::infinity();
  for (double bad : {nan, inf, -inf}) {
    auto c = center;
    c.lower.y = bad;
    check(!detail::boarding_route_checkpoint_world_frame_box_math(
              c, frame_identity(), {.24, .12, .18}),
          "Nonfinite center refuses before proxy arithmetic");
    auto R = frame_identity();
    R[1].upper.z = bad;
    check(!detail::boarding_route_checkpoint_world_frame_box_math(
              center, R, {.24, .12, .18}),
          "Nonfinite frame refuses before proxy arithmetic");
    check(!detail::boarding_route_checkpoint_world_frame_box_math(
              center, frame_identity(), {.24, bad, .18}),
          "Nonfinite original half dimension refuses");
  }
  auto R = frame_identity();
  R[0].lower.x = 2;
  check(!detail::boarding_route_checkpoint_world_frame_box_math(
            center, R, {.24, .12, .18}),
        "Unordered/invalid frame interval cannot hide original geometry");
  check(!detail::boarding_route_checkpoint_world_frame_box_math(
            center, frame_identity(), {.24, 0, .18}),
        "Nonpositive half cannot shrink body into a fake proxy");
  const PointBounds zero{{0, 0, 0}, {0, 0, 0}};
  const std::array<PointBounds, 3> huge{PointBounds{{8, 8, 8}, {8, 8, 8}},
                                        PointBounds{{8, 8, 8}, {8, 8, 8}},
                                        PointBounds{{8, 8, 8}, {8, 8, 8}}};
  const auto unsupported =
      require(detail::boarding_route_checkpoint_world_frame_box_math(
          zero, huge, {8, 8, 8}));
  check(!unsupported.arithmetic_supported && !unsupported.world_qualified,
        "Finite inputs whose enclosing proxy exceeds workspace refuse rather "
        "than clipping full geometry");
}
void sweep_controls() {
  using Math = detail::BoardingRouteCheckpointWorldSweepMath;
  auto denied = [](const Math& m) {
    check(!m.body_qualified && !m.source_qualified && !m.material_qualified &&
              !m.world_qualified && !m.actor_qualified,
          "Synthetic retained-cover arithmetic never supplies immutable source "
          "or WORLD permission");
  };
  auto interval = [](RigidVector3 lo, RigidVector3 hi) {
    auto b = box();
    b.first = b.second = {lo, hi};
    return b;
  };
  const std::array around{interval({-2, 0, 0}, {-.2, 0, 2}),
                          interval({-.2, 0, 2}, {.2, 0, 2}),
                          interval({.2, 0, 0}, {2, 0, 2})};
  const std::array<Triangle, 1> first{plane_x(0)};
  const auto clear = require(
      detail::boarding_route_checkpoint_world_sweep_math(around, first));
  denied(clear);
  check(clear.arithmetic_supported && clear.complete &&
            clear.work.triangle_union_pairs == 1 &&
            clear.work.refined_triangle_pairs == 3,
        "One unresolved immutable union requires every individually clear "
        "retained cell");
  const std::array consecutive{first[0], plane_x(-1, 0, 1)};
  const auto next = require(
      detail::boarding_route_checkpoint_world_sweep_math(around, consecutive));
  denied(next);
  check(!next.complete && next.refused_triangle == 1 &&
            next.refused_cell == 0 && next.work.triangle_union_pairs == 2 &&
            next.work.refined_triangle_pairs == 4,
        "NEXT triangle restores whole union rather than previous narrower "
        "final-cell workspace");
  const std::array middle{box({-2, 0, 0}), box(), box({2, 0, 0})};
  const auto collision = require(
      detail::boarding_route_checkpoint_world_sweep_math(middle, first));
  denied(collision);
  check(!collision.complete && collision.refused_cell == 1 &&
            collision.refused_triangle == 0,
        "Clear endpoints cannot hide middle-cell finite triangle overlap");
  const auto identity = frame_identity();
  auto quarter = identity;
  quarter[0] = {{0, 0, -1}, {0, 0, -1}};
  quarter[2] = {{1, 0, 0}, {1, 0, 0}};
  const PointBounds origin{{0, 0, 0}, {0, 0, 0}};
  const auto a = require(detail::boarding_route_checkpoint_world_frame_box_math(
                 origin, identity, {.4, .1, .1})),
             b = require(detail::boarding_route_checkpoint_world_frame_box_math(
                 origin, quarter, {.4, .1, .1}));
  const Triangle rotated_obstacle{RigidVector3{-.1, -.1, .3},
                                  RigidVector3{.1, -.1, .3},
                                  RigidVector3{0, .1, .3}};
  for (const auto& v : rotated_obstacle)
    check(std::abs(v.x) < .1 + 1e-12 && std::abs(v.y) <= .1 && v.z > .1 &&
              v.z < .4,
          "Independent rotation fixture lies outside endpoint box and "
          "intersects complete middle rotated box");
  const std::array rotation{a.solid, b.solid, a.solid};
  const std::array<Triangle, 1> rotation_faces{rotated_obstacle};
  const auto swept = require(detail::boarding_route_checkpoint_world_sweep_math(
      rotation, rotation_faces));
  denied(swept);
  check(!swept.complete && swept.refused_cell == 1,
        "Rotation-only protrusion in retained middle cell cannot be dropped by "
        "identity WORLD proxy");
  const Solid capsule{{{-.5, 0, 0}, {-.5, 0, 0}},
                      {{.5, 0, 0}, {.5, 0, 0}},
                      {},
                      .1,
                      Shape::capsule};
  for (double x : {-.55, .55}) {
    const Triangle cap{RigidVector3{x, -.04, -.04}, RigidVector3{x, .04, -.04},
                       RigidVector3{x, 0, .04}};
    const std::array cells{capsule};
    const std::array<Triangle, 1> faces{cap};
    const auto m = require(
        detail::boarding_route_checkpoint_world_sweep_math(cells, faces));
    denied(m);
    check(!m.complete && m.refused_cell == 0,
          "Complete finite capsule cap cannot be replaced by a cylinder-only "
          "WORLD reservation");
  }
  const std::array<Triangle, 1> far{plane_x(4)};
  Limits no_refinement;
  no_refinement.refined_triangle_pairs = 0;
  no_refinement.axes = 0;
  no_refinement.direction_entries_prepared = 0;
  const auto broad = require(detail::boarding_route_checkpoint_world_sweep_math(
      around, far, no_refinement));
  check(
      broad.complete && broad.work.refined_triangle_pairs == 0 &&
          broad.work.axes_examined == 0,
      "Actual immutable union separation needs no refined or directional work");
  for (int stage = 0; stage < 4; ++stage) {
    Limits l;
    Condition why{};
    if (stage == 0) {
      l.triangle_union_pairs = 0;
      why = Condition::pair_capacity;
    }
    if (stage == 1) {
      l.refined_triangle_pairs = 0;
      why = Condition::pair_capacity;
    }
    if (stage == 2) {
      l.axes = 0;
      why = Condition::axis_capacity;
    }
    if (stage == 3) {
      l.direction_entries_prepared = 0;
      why = Condition::preparation_capacity;
    }
    const auto m = require(
        detail::boarding_route_checkpoint_world_sweep_math(around, first, l));
    check(!m.complete && m.condition == why,
          "Genuine numeric fixture reaches each reduced work stage without an "
          "unrelated refusal masking it");
    denied(m);
  }
  Limits exact;
  exact.triangle_union_pairs = clear.work.triangle_union_pairs;
  exact.refined_triangle_pairs = clear.work.refined_triangle_pairs;
  const auto replay = require(
      detail::boarding_route_checkpoint_world_sweep_math(around, first, exact));
  check(replay.complete && replay.work.refined_triangle_pairs ==
                               clear.work.refined_triangle_pairs,
        "Exact used base/refinement budgets preserve full arithmetic cover");
  --exact.refined_triangle_pairs;
  const auto less = require(
      detail::boarding_route_checkpoint_world_sweep_math(around, first, exact));
  check(!less.complete && less.condition == Condition::pair_capacity &&
            less.work.refined_triangle_pairs == exact.refined_triangle_pairs,
        "One less actual refined comparison refuses before next original cell");
  auto invalid = around;
  invalid[0].first.lower.x = std::numeric_limits<double>::quiet_NaN();
  check(!detail::boarding_route_checkpoint_world_sweep_math(invalid, first),
        "Nonfinite numeric cell cannot enroll a surface interval");
  auto bad_triangle = first;
  bad_triangle[0][0].z = std::numeric_limits<double>::infinity();
  check(
      !detail::boarding_route_checkpoint_world_sweep_math(around, bad_triangle),
      "Nonfinite fake source face refuses before arithmetic sweep");
  const std::array<Solid, 9> too_many_cells{};
  const std::array<Triangle, 9> too_many_triangles{};
  check(!detail::boarding_route_checkpoint_world_sweep_math(too_many_cells,
                                                            first) &&
            !detail::boarding_route_checkpoint_world_sweep_math(
                around, too_many_triangles) &&
            !detail::boarding_route_checkpoint_world_sweep_math(
                std::span<const Solid>{}, first),
        "Numeric fixture buffers cannot exceed their eight-entry family or "
        "admit an empty cover");
}
struct HaloRecord {
  LowerCockpitTriangleKey key;
  std::uint32_t object{};
  std::string name;
  std::optional<std::uint32_t> evaluated;
  std::array<RigidVector3, 3> points;
  friend auto operator==(const HaloRecord&, const HaloRecord&)
      -> bool = default;
};
auto halo_record(const detail::LowerCockpitEffectiveTriangle& t) -> HaloRecord {
  return {t.key, t.object, std::string(t.source_object),
          t.evaluated_source_triangle, t.obstacle->points};
}
void halo_controls(const OriginBoardingBootSupport& boot) {
  const auto* contact = boot.contact();
  check(contact != nullptr,
        "Original source retains its admitted lower cockpit capability");
  if (!contact) return;
  std::vector<HaloRecord> reference;
  const auto old = require(detail::visit_effective_lower_cockpit_contact(
      *contact, [&](const auto& t) {
        if (t.key.buffer == LowerCockpitContactBuffer::halo)
          reference.push_back(halo_record(t));
        return true;
      }));
  check(old.complete && reference.size() == 8100,
        "Existing once-only effective source supplies authentic HALO roster "
        "independently");
  struct Collector {
    std::vector<HaloRecord> entries;
    bool stop{};
  };
  auto callback = [](void* raw,
                     const detail::LowerCockpitEffectiveTriangle& t) {
    auto& c = *static_cast<Collector*>(raw);
    c.entries.push_back(halo_record(t));
    return !c.stop;
  };
  Collector all;
  const auto full =
      require(detail::visit_lower_cockpit_halo(*contact, &all, callback));
  check(full.complete && full.metadata_complete && full.total_metadata == 75 &&
            full.metadata_examined == 75 && full.total_triangles == 8100 &&
            full.visited_triangles == 8100 && all.entries == reference,
        "Narrow iterator preserves all75 metadata records/8100 HALO "
        "faces/keys/names/vertices without originals or replacements");
  for (const auto& t : all.entries)
    check(t.key.buffer == LowerCockpitContactBuffer::halo && t.key.group == 0 &&
              t.evaluated.has_value(),
          "HALO namespace/evaluated-face attribution remains authentic");
  for (std::size_t cap : {std::size_t{0}, std::size_t{8099}}) {
    Collector c;
    const auto r = require(
        detail::visit_lower_cockpit_halo(*contact, &c, callback, 75, cap));
    check(!r.complete &&
              r.condition ==
                  detail::LowerCockpitHaloVisitCondition::triangle_capacity &&
              r.visited_triangles == cap && c.entries.size() == cap &&
              r.next_key == reference[cap].key,
          "HALO face cap refuses before NEXT unvisited genuine face");
  }
  for (std::size_t cap : {std::size_t{0}, std::size_t{74}}) {
    Collector c;
    const auto r = require(
        detail::visit_lower_cockpit_halo(*contact, &c, callback, cap, 8100));
    check(!r.complete && !r.metadata_complete &&
              r.condition ==
                  detail::LowerCockpitHaloVisitCondition::metadata_capacity &&
              r.metadata_examined == cap && r.next_metadata == cap &&
              c.entries.size() == r.visited_triangles,
          "HALO metadata cap records actual prefix and NEXT unvisited row");
    check(std::equal(c.entries.begin(), c.entries.end(), reference.begin()),
          "Reduced HALO metadata retains canonical genuine prefix");
  }
  Collector stop;
  stop.stop = true;
  const auto r =
      require(detail::visit_lower_cockpit_halo(*contact, &stop, callback));
  check(!r.complete &&
            r.condition ==
                detail::LowerCockpitHaloVisitCondition::callback_stopped &&
            r.visited_triangles == 1 &&
            stop.entries.front() == reference.front(),
        "Stopping callback's face is visited once and never mislabeled NEXT "
        "unattempted work");
  check(
      !detail::visit_lower_cockpit_halo(*contact, &all, nullptr) &&
          !detail::visit_lower_cockpit_halo(*contact, &all, callback, 76,
                                            8100) &&
          !detail::visit_lower_cockpit_halo(*contact, &all, callback, 75, 8101),
      "Missing visitor/raised HALO limits cannot enter source traversal");
  auto copy = *contact;
  const auto kept = std::move(copy);
  (void)kept;
  // NOLINTBEGIN(bugprone-use-after-move) -- Documented empty immutable source
  // must refuse HALO traversal.
  check(!detail::visit_lower_cockpit_halo(copy, &all, callback),
        "Moved HALO capability cannot supply source metadata");
  // NOLINTEND(bugprone-use-after-move) -- End empty-source contract check.
}
void finite_source_bridge() {
  const auto solid = box();
  const auto face = plane_x(20);
  check(!detail::boarding_source_endpoint_surface_checkpoint_pair_math(solid, 0,
                                                                       face),
        "Historical small numeric fixture does not admit authored source "
        "beyond its workspace");
  const auto result =
      require(detail::boarding_checkpoint_world_finite_triangle_pair_math(
          solid, 0, face));
  check(result.arithmetic_supported && result.certified &&
            !result.body_qualified && !result.self_qualified &&
            !result.surface_qualified && !result.world_qualified &&
            !result.route_qualified && !result.actor_qualified &&
            !result.sole_contact,
        "Finite-only source bridge reaches original kernel outside8m without "
        "granting caller/source/sole authority");
  auto bad = face;
  bad[2].x = std::numeric_limits<double>::quiet_NaN();
  check(!detail::boarding_checkpoint_world_finite_triangle_pair_math(solid, 0,
                                                                     bad),
        "Nonfinite authored source must refuse before projection");
  auto broken = solid;
  broken.first.lower.y = std::numeric_limits<double>::infinity();
  check(!detail::boarding_checkpoint_world_finite_triangle_pair_math(broken, 0,
                                                                     face),
        "Finite-source permission cannot bypass body workspace guard");
  check(!detail::boarding_checkpoint_world_finite_triangle_pair_math(solid, 0,
                                                                     face, 17),
        "Finite source cannot enlarge pair-axis ceiling");
}
void domain_controls(const OriginBoardingBootSupport& boot) {
  const auto* contact = boot.contact();
  if (!contact) return;
  const detail::CabinContactBox upper{{-.1, -.15, .8}, {.1, .1, 1}},
      lower{{-.1, -.3, -1}, {.1, -.25, -.8}}, hull{{-.1, -.3, -1}, {.1, .1, 1}};
  check(require(detail::covers_lower_cockpit_bounds(*contact, upper)) &&
            require(detail::covers_lower_cockpit_bounds(*contact, lower)) &&
            !require(detail::covers_lower_cockpit_bounds(*contact, hull)),
        "Genuine unchanged domain can cover every cell while conservative "
        "union fills its lower-front notch");
  const detail::CabinContactBox join{{0, -.2, 1}, {0, -.2, 1}},
      below{{0, std::nextafter(-.2, -1.), 1}, {0, std::nextafter(-.2, -1.), 1}};
  check(require(detail::covers_lower_cockpit_bounds(*contact, join)) &&
            !require(detail::covers_lower_cockpit_bounds(*contact, below)),
        "Exact closed domain join and adjacent-representable notch intrusion "
        "remain distinct without epsilon");
  auto bad = join;
  bad.low.x = std::numeric_limits<double>::quiet_NaN();
  check(!detail::covers_lower_cockpit_bounds(*contact, bad),
        "Nonfinite domain extent cannot acquire crop permission");
}
void domain_fallback_controls(const OriginBoardingBootSupport& boot) {
  const auto* contact = boot.contact();
  if (!contact) return;
  const std::array<PointBounds, 2> cells{
      PointBounds{{-.1, -.15, .8}, {.1, .1, 1}},
      PointBounds{{-.1, -.3, -1}, {.1, -.25, -.8}}};
  const auto m = require(
      detail::boarding_route_checkpoint_world_domain_math(*contact, cells));
  check(m.complete && m.work.domain_union_checks == 1 &&
            m.work.domain_cell_checks == 2 && m.work.proxy_preparations == 0 &&
            !m.body_qualified && !m.source_qualified && !m.world_qualified &&
            !m.material_qualified && !m.actor_qualified,
        "Actual shared domain fallback closes all original cells without "
        "converting caller AABBs into WORLD authority");
  for (std::uint64_t cap : {0U, 1U, 2U}) {
    Limits l;
    l.domain_cell_checks = cap;
    const auto r = require(detail::boarding_route_checkpoint_world_domain_math(
        *contact, cells, l));
    check(r.complete == (cap == 2) && r.work.domain_cell_checks == cap,
          "Zero/one-less/exact genuine domain checks stop before next retained "
          "bound");
    if (cap < 2)
      check(r.condition == Condition::domain_capacity && r.refused_cell == cap,
            "Domain capacity names NEXT original cell");
  }
  auto bad = cells;
  bad[1].upper.z = 1;
  const auto uncovered = require(
      detail::boarding_route_checkpoint_world_domain_math(*contact, bad));
  check(!uncovered.complete &&
            uncovered.condition == Condition::domain_uncovered &&
            uncovered.refused_cell == 1,
        "Genuine cell entering notch refuses after prior covered cell rather "
        "than repairing union or enlarging crop");
}
// Constructor fingerprint/ring/prism/cavity tests remain in the mandatory
// initial-material suite. These small controls preserve the authority boundary
// and distinguish containing material from an enclosing clear surface.
void material_and_sole_controls(const OriginBoardingBootSupport& boot) {
  const std::array<detail::MaterialPlane, 6> planes{
      detail::MaterialPlane{{1, 0, 0}, {1, 1, true}},
      detail::MaterialPlane{{-1, 0, 0}, {1, 1, true}},
      detail::MaterialPlane{{0, 1, 0}, {1, 1, true}},
      detail::MaterialPlane{{0, -1, 0}, {1, 1, true}},
      detail::MaterialPlane{{0, 0, 1}, {1, 1, true}},
      detail::MaterialPlane{{0, 0, -1}, {1, 1, true}}};
  auto unsupported_planes = planes;
  unsupported_planes[0].maximum.supported = false;
  check(
      !detail::initial_material_primitive_math(box(), 0, unsupported_planes, 6),
      "Unsupported caller plane bounds cannot enter material arithmetic");
  const auto inside =
      require(detail::initial_material_primitive_math(box(), 0, planes, 6));
  check(inside.arithmetic_supported && !inside.certified &&
            inside.axes_examined == 6 && !inside.material_qualified &&
            !inside.source_qualified && !inside.actor_qualified,
        "All six failed planes mean unresolved containing volume, even when "
        "its sheet surfaces are clear");
  const auto fewer =
      require(detail::initial_material_primitive_math(box(), 0, planes, 5));
  check(!fewer.certified && fewer.axes_examined == 5,
        "Actual six-plane relation cannot complete with one less finite plane");
  for (double x : {-1., 1.}) {
    const auto sheet =
        require(detail::boarding_checkpoint_world_finite_triangle_pair_math(
            box(), 0, plane_x(x)));
    check(sheet.certified && !sheet.world_qualified,
          "Clear source sheet alone does not establish outside of its filled "
          "enclosing primitive");
  }
  const auto cap = require(detail::initial_material_capsule_math(
      box({.58, 0, 0}, {.01, .01, .01}), 0, {{-.5, 0, 0}, {-.5, 0, 0}},
      {{.5, 0, 0}, {.5, 0, 0}}, .1));
  check(cap.arithmetic_supported && !cap.certified && !cap.material_qualified &&
            !cap.actor_qualified,
        "Conservative extra-endcap overlap may refuse without diagnosing real "
        "source penetration");
  const auto source = boot.selected_partitions();
  if (source.empty()) return;
  const double plane = source.front().plane_metres;
  const Triangle all{RigidVector3{-.01, plane, -.01},
                     RigidVector3{.01, plane, -.01},
                     RigidVector3{0, plane, .01}};
  const auto sole =
      require(detail::boarding_source_endpoint_surface_checkpoint_sole_math(
          {plane, .05, -.847}, .847, .05, all));
  check(sole.arithmetic_supported && sole.template_matches &&
            sole.all_vertices_at_or_below && !sole.body_qualified &&
            !sole.world_qualified && !sole.actor_qualified,
        "Exact original plane expression tests every face vertex without "
        "granting fake sole/source permission");
  for (std::size_t vertex = 0; vertex < 3; ++vertex) {
    auto raised = all;
    raised[vertex].y = std::nextafter(plane, 1.);
    const auto no =
        require(detail::boarding_source_endpoint_surface_checkpoint_sole_math(
            {plane, .05, -.847}, .847, .05, raised));
    check(!no.all_vertices_at_or_below && !no.world_qualified,
          "One adjacent-representable raised vertex forbids blanket sole "
          "exemption");
  }
}
void denied(const Diagnostic& d) {
  check(!d.route_qualified && !d.actor_qualified && !d.seat_qualified &&
            !d.save_qualified && !d.first_flight_qualified &&
            !d.free_foot_swing_qualified && !d.dynamics_qualified &&
            !d.strength_qualified && !d.friction_qualified,
        "Registered craft material and HALO exclusion is not actor, seat, "
        "dynamics or complete route permission");
}
void observe(std::string_view label, const Diagnostic& d) {
  const auto& p = d.world;
  const auto& w = p.work;
  std::cout << std::setprecision(17) << label << " complete=" << p.complete
            << " material=" << p.material_exclusion
            << " halo=" << p.halo_surface_exclusion
            << " domain=" << p.domain_complete;
  if (d.child)
    std::cout << " child=" << d.child->complete
              << " cells=" << d.child->cells.size();
  std::cout << " work=" << w.roster_entries << ',' << w.effective_sources << ','
            << w.union_envelope_pairs << ',' << w.union_proxy_preparations
            << ',' << w.domain_union_checks << ',' << w.domain_cell_checks
            << ',' << w.proxy_preparations << ',' << w.refined_envelope_pairs
            << ',' << w.material_triangle_visits << ','
            << w.halo_metadata_visits << ',' << w.halo_triangle_visits << ','
            << w.collapsed_triangles << ',' << w.triangle_union_pairs << ','
            << w.refined_triangle_pairs << ',' << w.base_enclosure_relations
            << ',' << w.refined_enclosure_relations << ',' << w.axes_examined
            << ',' << w.direction_entries_prepared << ','
            << w.sole_vertex_guards << ',' << w.sole_triangle_exclusions << ','
            << w.sole_volume_exclusions;
  if (p.first_refusal) {
    const auto& r = *p.first_refusal;
    std::cout << " refusal=" << static_cast<unsigned>(r.condition)
              << " object=" << r.source_object
              << " relation=" << static_cast<unsigned>(r.relation)
              << " interval=" << r.global_first << ',' << r.global_last
              << " phase=" << r.phase_index;
    if (r.source) std::cout << " source=" << *r.source;
    if (r.part) std::cout << " part=" << *r.part;
    if (r.cell) std::cout << " cell=" << *r.cell;
    if (r.triangle) std::cout << " triangle=" << *r.triangle;
    if (r.source_key)
      std::cout << " key=" << static_cast<unsigned>(r.source_key->buffer) << ','
                << r.source_key->group << ',' << r.source_key->triangle;
  }
  std::cout << '\n';
  std::cout.flush();
}
struct Cap {
  std::uint64_t BoardingRouteCheckpointWorldCounters::* work;
  std::uint64_t Limits::* limit;
  Condition condition;
  bool conservative{};
};
constexpr std::array caps{
    Cap{&BoardingRouteCheckpointWorldCounters::roster_entries,
        &Limits::roster_entries, Condition::roster_capacity},
    Cap{&BoardingRouteCheckpointWorldCounters::effective_sources,
        &Limits::effective_sources, Condition::roster_capacity},
    Cap{&BoardingRouteCheckpointWorldCounters::union_envelope_pairs,
        &Limits::union_envelope_pairs, Condition::envelope_capacity},
    Cap{&BoardingRouteCheckpointWorldCounters::union_proxy_preparations,
        &Limits::union_proxy_preparations, Condition::proxy_capacity},
    Cap{&BoardingRouteCheckpointWorldCounters::domain_union_checks,
        &Limits::domain_union_checks, Condition::domain_capacity},
    Cap{&BoardingRouteCheckpointWorldCounters::domain_cell_checks,
        &Limits::domain_cell_checks, Condition::domain_capacity},
    Cap{&BoardingRouteCheckpointWorldCounters::proxy_preparations,
        &Limits::proxy_preparations, Condition::proxy_capacity},
    Cap{&BoardingRouteCheckpointWorldCounters::refined_envelope_pairs,
        &Limits::refined_envelope_pairs, Condition::envelope_capacity},
    Cap{&BoardingRouteCheckpointWorldCounters::material_triangle_visits,
        &Limits::material_triangle_visits, Condition::triangle_capacity},
    Cap{&BoardingRouteCheckpointWorldCounters::halo_metadata_visits,
        &Limits::halo_metadata_visits, Condition::roster_capacity},
    Cap{&BoardingRouteCheckpointWorldCounters::halo_triangle_visits,
        &Limits::halo_triangle_visits, Condition::triangle_capacity},
    Cap{&BoardingRouteCheckpointWorldCounters::triangle_union_pairs,
        &Limits::triangle_union_pairs, Condition::pair_capacity},
    Cap{&BoardingRouteCheckpointWorldCounters::refined_triangle_pairs,
        &Limits::refined_triangle_pairs, Condition::pair_capacity},
    Cap{&BoardingRouteCheckpointWorldCounters::base_enclosure_relations,
        &Limits::base_enclosure_relations, Condition::pair_capacity},
    Cap{&BoardingRouteCheckpointWorldCounters::refined_enclosure_relations,
        &Limits::refined_enclosure_relations, Condition::pair_capacity},
    Cap{&BoardingRouteCheckpointWorldCounters::axes_examined, &Limits::axes,
        Condition::axis_capacity, true},
    Cap{&BoardingRouteCheckpointWorldCounters::direction_entries_prepared,
        &Limits::direction_entries_prepared, Condition::preparation_capacity,
        true},
    Cap{&BoardingRouteCheckpointWorldCounters::sole_vertex_guards,
        &Limits::sole_vertex_guards, Condition::sole_guard_capacity}};
void accounting(const Diagnostic& d, const Limits& l = {}) {
  denied(d);
  const auto& p = d.world;
  const auto& w = p.work;
  check(p.world_version == 1 && p.output_capacity_bytes <= l.output_bytes,
        "Fixed WORLD payload/version and actually owned output stay bounded");
  for (const auto& c : caps)
    check(w.*c.work <= l.*c.limit, "Actual WORLD operations never exceed their "
                                   "individual registered ceilings");
  check(w.collapsed_triangles <= w.material_triangle_visits &&
            w.direction_entries_prepared % 16 == 0 &&
            w.sole_vertex_guards <= 192,
        "Collapse/prepared-list/once-only sole vertex work preserves actual "
        "semantics");
  if (d.child) {
    check(w.union_proxy_preparations <= 15 * d.child->cells.size() &&
              w.domain_union_checks <= 15 &&
              w.domain_cell_checks <= 15 * d.child->cells.size(),
          "Union and fallback domain obligations visit only original retained "
          "cells");
    check(w.proxy_preparations <=
              w.union_proxy_preparations + w.domain_cell_checks +
                  w.refined_envelope_pairs + w.refined_triangle_pairs +
                  w.refined_enclosure_relations,
          "Refined proxy construction charges actual originating obligations "
          "without hidden per-cell cache");
    if (p.first_refusal && p.first_refusal->cell) {
      const auto& r = *p.first_refusal;
      check(*r.cell < d.child->cells.size(),
            "Refusal names original retained-cell index");
      if (*r.cell < d.child->cells.size()) {
        const auto& c = d.child->cells[*r.cell];
        check(r.global_first == c.global_first &&
                  r.global_last == c.global_last &&
                  r.phase_index == c.phase_index,
              "Refusal preserves exact global interval and phase of unchanged "
              "child");
      }
    }
  }
  if (p.complete) {
    check(!p.first_refusal && d.child && d.child->complete &&
              p.bindings_complete && p.source_complete &&
              p.arithmetic_supported && p.domain_complete &&
              p.material_exclusion && p.halo_surface_exclusion,
          "Completion requires entire genuine child/material/HALO/domain "
          "obligations");
    check(w.roster_entries == 1759 && w.effective_sources == 1751 &&
              w.union_envelope_pairs == 26265 && w.halo_metadata_visits == 75 &&
              w.halo_triangle_visits == 8100,
          "Complete WORLD record accounts all eight removals/thirteen "
          "replacements and genuine HALO roster");
    for (const auto& part : p.parts)
      check(part.complete && part.domain_complete &&
                part.original_world_identity &&
                part.material_sources_closed == 1751 && d.child &&
                part.material_cells_closed == 1751 * d.child->cells.size() &&
                part.halo_triangles_closed == 8100,
            "Every complete part closes all effective material and HALO "
            "obligations");
  } else
    check(p.first_refusal.has_value(),
          "Incomplete WORLD result retains explicit bounded source or "
          "prerequisite refusal");
  if (p.first_refusal && p.first_refusal->source) {
    const auto& r = *p.first_refusal;
    check(r.source_object == d.source.source_name(*r.source),
          "Material refusal retains authentic immutable source name");
    if (r.condition == Condition::missing_relation)
      check(r.relation == BoardingInitialMaterialRelation::unknown ||
                r.relation == BoardingInitialMaterialRelation::stowed,
            "Unknown material remains explicit blocker instead of a shell/sole "
            "exemption");
  }
}
void invalid_controls(const OriginBoardingBootSupport& boot,
                      const OriginBoardingInitialMaterial& material) {
  for (double v :
       {-1., std::nextafter(1., 2.), std::numeric_limits<double>::quiet_NaN(),
        std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity()})
    check(!assess_origin_boarding_route_checkpoint_world_material(
              boot, material, v, 1) &&
              !assess_origin_boarding_route_checkpoint_world_material(
                  boot, material, 0, v),
          "Invalid global clock refuses before source or body work");
  auto copied_boot = boot;
  const auto kept_boot = std::move(copied_boot);
  (void)kept_boot;
  // NOLINTBEGIN(bugprone-use-after-move) -- Documented empty boot capability
  // must refuse WORLD admission.
  check(!assess_origin_boarding_route_checkpoint_world_material(copied_boot,
                                                                material),
        "Moved source cannot bind WORLD consumer");
  // NOLINTEND(bugprone-use-after-move) -- End empty-boot contract check.
  auto copied_material = material;
  const auto kept_material = std::move(copied_material);
  (void)kept_material;
  // NOLINTBEGIN(bugprone-use-after-move) -- Documented empty material
  // capability must refuse WORLD admission.
  check(!assess_origin_boarding_route_checkpoint_world_material(
            boot, copied_material),
        "Moved material cannot supply immutable constructor authority");
  // NOLINTEND(bugprone-use-after-move) -- End empty-material contract check.
  for (const auto& c : caps) {
    Limits l;
    ++(l.*c.limit);
    check(!detail::boarding_route_checkpoint_world_bounded(boot, material, 0, 1,
                                                           l),
          "No new world stage can raise its registered ceiling");
  }
  Limits l;
  l.output_bytes = 0;
  const auto empty = require(
      detail::boarding_route_checkpoint_world_bounded(boot, material, 0, 1, l));
  check(
      !empty.world.complete && empty.world.first_refusal &&
          empty.world.first_refusal->condition == Condition::output_capacity &&
          empty.world.work.proxy_preparations == 0 &&
          empty.world.work.triangle_union_pairs == 0,
      "Too-small aggregate output refuses before child or WORLD scratch work");
  denied(empty);
  l = {};
  l.child.phase.nodes = 0;
  const auto child = require(
      detail::boarding_route_checkpoint_world_bounded(boot, material, 0, 1, l));
  check(child.child && !child.child->complete && !child.world.complete &&
            child.world.first_refusal &&
            child.world.first_refusal->condition ==
                Condition::child_prerequisite &&
            child.world.work.union_proxy_preparations == 0 &&
            child.world.work.union_envelope_pairs == 0,
        "Actual incomplete owned child blocks all new geometry rather than "
        "accepting caller flags");
  Diagnostic stale(material);
  stale.world.complete = stale.world.material_exclusion =
      stale.world.halo_surface_exclusion = stale.world.domain_complete =
          stale.world.arithmetic_supported = true;
  stale.world.work.proxy_preparations =
      std::numeric_limits<std::uint64_t>::max();
  check(!detail::prepare_boarding_route_checkpoint_world(boot, material, -1, 1,
                                                         Limits{}, stale) &&
            !stale.world.complete && !stale.world.material_exclusion &&
            !stale.world.halo_surface_exclusion &&
            !stale.world.domain_complete &&
            stale.world.work.proxy_preparations == 0,
        "Invalid private preparation clears stale flags and forged incoming "
        "progress before any work");
}
void unsafe_environment(const OriginBoardingBootSupport& boot,
                        const OriginBoardingInitialMaterial& material) {
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
    const PointBounds c{{0, 0, 0}, {0, 0, 0}};
    const auto p = detail::boarding_route_checkpoint_world_frame_box_math(
        c, frame_identity(), {.24, .12, .18});
    check(!p || (!p->arithmetic_supported && !p->world_qualified &&
                 !p->sole_qualified),
          "Unsafe environment never certifies frame expression or raw sole "
          "authority");
    const auto m = detail::boarding_checkpoint_world_finite_triangle_pair_math(
        box(), 0, plane_x(20));
    check(!m || (!m->arithmetic_supported && !m->certified &&
                 !m->world_qualified && !m->sole_contact),
          "Finite-only source wrapper cannot bypass FP guard");
    const auto d =
        assess_origin_boarding_route_checkpoint_world_material(boot, material);
    check(!d || (!d->world.complete && !d->world.arithmetic_supported &&
                 !d->world.material_exclusion &&
                 !d->world.halo_surface_exclusion),
          "Unsafe FP mode cannot retain genuine material/HALO qualification");
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
void actual_proxy_oracles(const OriginBoardingBootSupport& boot,
                          const OriginBoardingInitialMaterial& material,
                          const Diagnostic& observed) {
  Diagnostic owner(material);
  auto prepared = detail::prepare_boarding_route_checkpoint_world(
      boot, material, observed.child->requested_first,
      observed.child->requested_last, Limits{}, owner);
  if (!prepared) {
    std::cout << "AUTHENTIC_PROXY_CONTEXT API_ERROR=" << prepared.error()
              << '\n';
    std::cout.flush();
  }
  auto context = require(std::move(prepared));
  check(owner.child && snapshot(*owner.child) == snapshot(*observed.child),
        "Private issuer owns the same genuine graph/control/source cover "
        "rather than admitting caller report");
  check(Access::valid(context) && Access::source_count(context) == 1759,
        "Purpose-private source bridge retains original immutable material "
        "handle");
  std::size_t removed{}, replacements{}, support_matches{};
  for (std::size_t source = 0; source < Access::source_count(context);
       ++source) {
    const auto* r = Access::source(context, source);
    check(r != nullptr && r->name == material.source_name(source),
          "Every borrowed material row keeps authenticated source identity");
    if (!r) continue;
    const auto bounds = Access::envelope(context, source);
    const auto original =
        detail::initial_material_source_envelope(material, source);
    check(
        bounds.has_value() == original.has_value(),
        "Moving/removed/replacement source envelope availability is unchanged");
    if (bounds && original)
      check(bounds->lower == original->lower &&
                bounds->upper == original->upper,
            "Effective world envelope retains owned C++ rest/captured union "
            "and once-only owner transform");
    removed += r->removed ? 1U : 0U;
    replacements += r->replacement ? 1U : 0U;
    for (std::size_t side = 0; side < 2; ++side)
      if (Access::sole_volume_clear(context, side == 0 ? 5 : 11, source)) {
        ++support_matches;
        check(r->relation ==
                      BoardingInitialMaterialRelation::support_enclosure &&
                  !r->removed,
              "Volume sole exception belongs only to selected complete support "
              "constructor");
        check(!Access::sole_volume_clear(context, side == 0 ? 11 : 5, source),
              "Port and starboard complete-object permissions cannot be "
              "exchanged");
      }
    check(!Access::sole_volume_clear(context, 0, source),
          "Pelvis cannot inherit boot material exemption");
  }
  check(removed == 8 && replacements == 13 && support_matches == 2 &&
            owner.world.work.sole_vertex_guards == 192,
        "All removals/replacements and two complete96-vertex sole guards are "
        "retained once");
  std::array<PointBounds, 15> union_bounds{};
  for (std::size_t cell = 0; cell < owner.child->cells.size(); ++cell) {
    const auto& phase = owner.child->cells[cell].assessment.phase;
    std::size_t boxes{}, capsules{};
    std::uint32_t total_weight{};
    for (std::size_t part = 0; part < 15; ++part) {
      const auto p = require(
          detail::boarding_route_checkpoint_world_proxy(context, part, cell));
      check(p.arithmetic_supported && p.original_world_identity,
            "Actual original part acquires full conservative WORLD proxy only "
            "from opaque fresh issuer");
      const auto& binding = owner.child->parts[part];
      check(static_cast<std::size_t>(binding.id) == part,
            "WORLD canonical part order remains unchanged");
      total_weight += binding.mass.weight;
      if (const auto* b =
              std::get_if<BoardingRoutePhaseBoxBinding>(&binding.reservation)) {
        ++boxes;
        std::array<PointBounds, 3> columns;
        for (std::size_t j = 0; j < 3; ++j)
          columns[j] =
              phase.frames[static_cast<std::size_t>(b->frame)].columns[j].value;
        corner_oracle(phase.points[static_cast<std::size_t>(b->center)].value,
                      columns, b->half_size_metres, p.solid, p.bounds);
        const RigidVector3 expected =
            part == 0                 ? RigidVector3{.24, .12, .18}
            : part == 1               ? RigidVector3{.26, .2695, .18}
            : part == 2               ? RigidVector3{.16, .18, .18}
            : part == 5 || part == 11 ? RigidVector3{.06, .05, .14}
                                      : RigidVector3{.04, .05, .02};
        check(b->half_size_metres == expected,
              "Trunk/helmet and all other WORLD boxes retain exact original "
              "full half dimensions");
      } else {
        ++capsules;
        const auto& c =
            std::get<BoardingPlantedBodyCapsuleBinding>(binding.reservation);
        const auto first =
                       phase.points[static_cast<std::size_t>(c.start)].value,
                   last = phase.points[static_cast<std::size_t>(c.end)].value;
        check(p.solid.shape == Shape::capsule &&
                  p.solid.radius_metres == c.radius_metres &&
                  p.solid.first.lower == first.lower &&
                  p.solid.first.upper == first.upper &&
                  p.solid.second.lower == last.lower &&
                  p.solid.second.upper == last.upper,
              "Full original capsule keeps both actual interval endpoints and "
              "unchanged radius");
        for (std::size_t i = 0; i < 3; ++i)
          for (auto endpoint : std::array{first, last}) {
            check(
                contains(
                    component(p.bounds.lower, i), component(p.bounds.upper, i),
                    static_cast<long double>(component(endpoint.lower, i)) -
                        c.radius_metres) &&
                    contains(
                        component(p.bounds.lower, i),
                        component(p.bounds.upper, i),
                        static_cast<long double>(component(endpoint.upper, i)) +
                            c.radius_metres),
                "Independent complete capsule endpoint-ball extrema retained "
                "in WORLD enclosure");
          }
      }
      if (part == 5 || part == 11) {
        const auto side = part == 5 ? 0U : 1U;
        const auto plane =
            owner.child->initial->self.endpoint.legs[side].plane_metres;
        check(p.sole_expression_identity && p.sole_plane == plane &&
                  p.bounds.lower.y == plane,
              "Stationary boot retains exact original source-plane expression "
              "rather than rounded center subtraction");
      } else
        check(!p.sole_expression_identity,
              "Other parts cannot borrow fixed boot source identity");
      if (cell == 0)
        union_bounds[part] = p.bounds;
      else {
        auto& u = union_bounds[part];
        u.lower = {std::min(u.lower.x, p.bounds.lower.x),
                   std::min(u.lower.y, p.bounds.lower.y),
                   std::min(u.lower.z, p.bounds.lower.z)};
        u.upper = {std::max(u.upper.x, p.bounds.upper.x),
                   std::max(u.upper.y, p.bounds.upper.y),
                   std::max(u.upper.z, p.bounds.upper.z)};
      }
    }
    check(boxes == 7 && capsules == 8 && total_weight == 1200,
          "Current WORLD roster retains seven full boxes/eight full capsules "
          "and full1200 mass");
  }
  const auto* contact = boot.contact();
  for (std::size_t part = 0; part < 15; ++part) {
    const auto& reported = observed.world.parts[part];
    if (!reported.original_world_identity) continue;
    check(reported.trajectory_union.lower == union_bounds[part].lower &&
              reported.trajectory_union.upper == union_bounds[part].upper,
          "Reported motion union exactly combines every retained full-frame "
          "cell proxy; refined workspace cannot overwrite it");
    if (contact)
      check(reported.union_domain_complete ==
                require(detail::covers_lower_cockpit_bounds(
                    *contact,
                    {union_bounds[part].lower, union_bounds[part].upper})),
            "Reported union-domain predicate preserves authentic original "
            "notch semantics");
  }
  check(!detail::boarding_route_checkpoint_world_proxy(context, 15, 0) &&
            !detail::boarding_route_checkpoint_world_proxy(
                context, 0, owner.child->cells.size()),
        "Out-of-range private body/cell indices cannot admit geometry");
  auto moved = std::move(context);
  check(Access::valid(moved),
        "Genuine opaque context remains valid in moved destination");
  // NOLINTBEGIN(bugprone-use-after-move) -- Empty moved context must refuse its
  // borrowed source/body views.
  check(!Access::valid(context) &&
            !detail::boarding_route_checkpoint_world_proxy(context, 0, 0),
        "Moved context cannot retain stale body or material permission");
  // NOLINTEND(bugprone-use-after-move) -- End empty-context contract check.
  auto moved_owner = std::move(owner);
  check(!Access::valid(moved) &&
            !detail::boarding_route_checkpoint_world_proxy(moved, 0, 0),
        "Moving the owning report invalidates old private borrowing context "
        "without dereferencing freed child");
  check(moved_owner.child &&
            snapshot(*moved_owner.child) == snapshot(*observed.child),
        "Public moved report retains original owned child and source data");
  Diagnostic reset_owner(material);
  auto reset_context = require(detail::prepare_boarding_route_checkpoint_world(
      boot, material, 0, 0, Limits{}, reset_owner));
  reset_owner.child->cells.clear();
  check(!Access::valid(reset_context) &&
            !detail::boarding_route_checkpoint_world_proxy(reset_context, 0, 0),
        "Changed accepted-vector identity/count invalidates the private "
        "retained cover");
  reset_owner.child.reset();
  check(!Access::valid(reset_context) &&
            Access::source_count(reset_context) == 0 &&
            !detail::boarding_route_checkpoint_world_proxy(reset_context, 0, 0),
        "Destroyed owned child refuses before dereferencing borrowed cells "
        "while parent remains alive");
}
void budgets(const OriginBoardingBootSupport& boot,
             const OriginBoardingInitialMaterial& material) {
  const auto base =
      require(assess_origin_boarding_route_checkpoint_world_material(
          boot, material, 0, 0));
  observe("POINT_BUDGET_BASE", base);
  accounting(base);
  for (const auto& c : caps) {
    const auto used = base.world.work.*c.work;
    if (used == 0) continue;
    Limits exact;
    exact.*c.limit = used;
    const auto replay = require(detail::boarding_route_checkpoint_world_bounded(
        boot, material, 0, 0, exact));
    accounting(replay, exact);
    if (c.conservative && replay.world.first_refusal &&
        replay.world.first_refusal->condition == c.condition) {
      check(!replay.world.complete,
            "Registered conservative eager-list admission may refuse after "
            "last actual charged operation");
    } else {
      check(replay.world.complete == base.world.complete &&
                replay.world.first_refusal.has_value() ==
                    base.world.first_refusal.has_value(),
            "Exact reached work cap preserves baseline completion/refusal "
            "outcome");
      if (base.world.first_refusal && replay.world.first_refusal)
        check(replay.world.first_refusal->condition ==
                      base.world.first_refusal->condition &&
                  replay.world.first_refusal->source ==
                      base.world.first_refusal->source &&
                  replay.world.first_refusal->cell ==
                      base.world.first_refusal->cell,
              "Exact actual work budget retains original source/cell refusal "
              "rather than fabricating success");
    }
    check((replay.world.work.*c.work) <= used,
          "Exact operation cap never records invented work");
    --(exact.*c.limit);
    const auto less = require(detail::boarding_route_checkpoint_world_bounded(
        boot, material, 0, 0, exact));
    accounting(less, exact);
    check(!less.world.complete && less.world.first_refusal &&
              less.world.first_refusal->condition == c.condition,
          "One less genuinely executed operation reaches its corresponding "
          "capacity refusal");
    Limits zero;
    zero.*c.limit = 0;
    const auto empty = require(detail::boarding_route_checkpoint_world_bounded(
        boot, material, 0, 0, zero));
    check(!empty.world.complete && empty.world.first_refusal &&
              empty.world.first_refusal->condition == c.condition &&
              (empty.world.work.*c.work) == 0,
          "Independent zero reached-stage cap refuses before actual operation "
          "rather than being masked");
    accounting(empty, zero);
  }
}
void observed_complete(const Diagnostic& d) {
  check(d.world.complete && !d.world.first_refusal && d.child &&
            d.child->complete && d.world.bindings_complete &&
            d.world.source_complete && d.world.arithmetic_supported &&
            d.world.domain_complete && d.world.material_exclusion &&
            d.world.halo_surface_exclusion,
        "Frozen public observation requires actual complete material, HALO and "
        "domain exclusion");
  check(d.world.work.roster_entries == 1759 &&
            d.world.work.effective_sources == 1751 &&
            d.world.work.halo_metadata_visits == 75 &&
            d.world.work.halo_triangle_visits == 8100,
        "Observed complete request retains entire authenticated material and "
        "HALO rosters");
}
void observed_missing(const Diagnostic& d, std::size_t source,
                      std::string_view name, std::size_t cell, double first,
                      double last, std::size_t phase) {
  check(!d.world.complete && !d.world.material_exclusion &&
            !d.world.halo_surface_exclusion &&
            d.world.first_refusal.has_value(),
        "Observed missing relation remains explicit refusal without whole "
        "material or HALO success");
  if (!d.world.first_refusal) return;
  const auto& r = *d.world.first_refusal;
  check(r.condition == Condition::missing_relation && r.source == source &&
            r.source_object == name && r.part == 12 && r.cell == cell &&
            r.global_first == first && r.global_last == last &&
            r.phase_index == phase &&
            (r.relation == BoardingInitialMaterialRelation::unknown ||
             r.relation == BoardingInitialMaterialRelation::stowed),
        "Frozen refusal retains genuine source, starboard upper arm, exact "
        "retained interval and missing relation");
}
void whole_reverse_identity(const Diagnostic& f, const Diagnostic& r) {
  check(f.world.first_refusal && r.world.first_refusal,
        "Observed whole/reverse both retain material refusal");
  if (f.world.first_refusal && r.world.first_refusal) {
    const auto& a = *f.world.first_refusal;
    const auto& b = *r.world.first_refusal;
    check(a.condition == b.condition && a.part == b.part &&
              a.source == b.source && a.cell == b.cell &&
              a.triangle == b.triangle && a.source_key == b.source_key &&
              a.source_object == b.source_object && a.relation == b.relation &&
              a.global_first == b.global_first &&
              a.global_last == b.global_last && a.phase_index == b.phase_index,
          "Whole/reverse preserve exact canonical source and original-cell "
          "refusal identity");
  }
  for (std::size_t part = 0; part < 15; ++part)
    check(f.world.parts[part].trajectory_union.lower ==
                  r.world.parts[part].trajectory_union.lower &&
              f.world.parts[part].trajectory_union.upper ==
                  r.world.parts[part].trajectory_union.upper,
          "Reverse preserves immutable full-frame WORLD motion unions");
  for (const auto& cap : caps)
    check(f.world.work.*cap.work == r.world.work.*cap.work,
          "Whole/reverse actual world work is identical without freezing its "
          "count");
  check(f.world.work.collapsed_triangles == r.world.work.collapsed_triangles &&
            f.world.work.sole_triangle_exclusions ==
                r.world.work.sole_triangle_exclusions &&
            f.world.work.sole_volume_exclusions ==
                r.world.work.sole_volume_exclusions,
        "Whole/reverse preserve remaining actual collapsed/sole work");
}
void public_cases(const OriginBoardingBootSupport& boot,
                  const OriginBoardingInitialMaterial& material) {
  const auto old = snapshot(
      require(assess_origin_boarding_route_checkpoint_unload_self02(boot)));
  auto result =
      assess_origin_boarding_route_checkpoint_world_material(boot, material);
  if (!result) {
    std::cout << "FIRST_PUBLIC_CHECKPOINT_WORLD_MATERIAL API_ERROR="
              << result.error() << '\n';
    std::cout.flush();
  }
  auto first = require(std::move(result));
  observe("FIRST_PUBLIC_CHECKPOINT_WORLD_MATERIAL", first);
  accounting(first);
  // Frozen629e254 GCC/Clang20 FIRST observations preceded these regressions.
  observed_missing(first, 1436, "WF02 | CABIN emergency pressure frame", 107,
                   .6796875, .6875, 2);
  check(first.child && snapshot(*first.child) == old,
        "New WORLD consumer retains one unchanged freshly issued Self02 child "
        "fieldwise");
  if (first.child && first.child->complete)
    actual_proxy_oracles(boot, material, first);
  budgets(boot, material);
  // Preserve each FIRST receipt before checking its now-known observed result.
  for (auto range : std::array<std::array<double, 2>, 8>{{{1, 0},
                                                          {0, .25},
                                                          {.25, .5},
                                                          {.5, 1},
                                                          {.125, .75},
                                                          {.75, .125},
                                                          {0, 0},
                                                          {1, 1}}}) {
    const auto d =
        require(assess_origin_boarding_route_checkpoint_world_material(
            boot, material, range[0], range[1]));
    observe("REQUEST", d);
    accounting(d);
    if (range[0] == 1 && range[1] == 0) {
      observed_missing(d, 1436, "WF02 | CABIN emergency pressure frame", 107,
                       .6796875, .6875, 2);
      whole_reverse_identity(first, d);
    }
    if ((range[0] == 0 && range[1] == .25) || (range[0] == 0 && range[1] == 0))
      observed_complete(d);
    if (range[0] == .25 && range[1] == .5)
      observed_missing(d, 1574, "WF02 | retained nose joint backing", 3,
                       .359375, .375, 1);
    if (range[0] == 1 && range[1] == 1)
      observed_missing(d, 1436, "WF02 | CABIN emergency pressure frame", 0, 1,
                       1, 2);
    const auto original =
        require(assess_origin_boarding_route_checkpoint_unload_self02(
            boot, range[0], range[1]));
    check(d.child && snapshot(*d.child) == snapshot(original),
          "Every new reverse/sub/point owns exact original controls, closed "
          "cover, physical clock, joins and source evidence");
    if (d.world.complete)
      check(d.world.material_exclusion && d.world.halo_surface_exclusion &&
                d.world.domain_complete,
            "Conditional success never loses distinct material/HALO/domain "
            "obligations");
  }
  const auto refusal = first.world.first_refusal;
  const auto name =
      refusal ? std::string(refusal->source_object) : std::string{};
  auto moved = std::move(first);
  check(moved.child && snapshot(*moved.child) == old &&
            (!moved.world.first_refusal ||
             moved.world.first_refusal->source_object == name),
        "Public result move retains child and borrowed stopping source names");
  auto changed = std::move(moved);
  changed.world.complete = changed.world.material_exclusion =
      changed.world.halo_surface_exclusion = true;
  if (changed.child && !changed.child->cells.empty())
    changed.child->cells.front().assessment.phase.points[0].value.lower.x =
        -999;
  check(snapshot(require(assess_origin_boarding_route_checkpoint_unload_self02(
            boot))) == old,
        "Mutable returned flags/body report cannot alter immutable genuine "
        "producer or enroll future WORLD authority");
}
} // namespace
int main() {
  try {
    static_assert(sizeof(BoardingRouteCheckpointWorldPayload) <= 4096);
    static_assert(
        !std::is_constructible_v<detail::BoardingRouteCheckpointWorldContext,
                                 const Child&,
                                 const OriginBoardingInitialMaterial::Data*>);
    const auto binding = require(make_native_starting_assembly_binding(
        NativeStartingAssemblySelection{}));
    const auto boot = require(make_origin_boarding_boot_support(binding));
    auto admitted = make_origin_boarding_initial_material(binding);
    if (!admitted) {
      std::cout << "MATERIAL_CAPABILITY API_ERROR=" << admitted.error() << '\n';
      std::cout.flush();
      throw std::runtime_error("Required WORLD/material API refused: " +
                               admitted.error());
    }
    const auto& material = *admitted;
    invalid_controls(boot, material);
    unsafe_environment(boot, material);
    frame_controls();
    finite_source_bridge();
    sweep_controls();
    halo_controls(boot);
    domain_controls(boot);
    domain_fallback_controls(boot);
    material_and_sole_controls(boot);
    public_cases(boot, material);
  } catch (const std::exception& e) {
    ++failures;
    std::cerr << "EXCEPTION: " << e.what() << '\n';
  }
  std::cout << checks << " checks, " << failures << " failures\n";
  return failures == 0 ? 0 : 1;
}
