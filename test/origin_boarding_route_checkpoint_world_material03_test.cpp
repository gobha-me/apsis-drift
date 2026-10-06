#include "apsis_drift/origin_boarding_route_checkpoint_world_material03.hpp"
#include "origin_boarding_checkpoint_material_extension_internal.hpp"
#include "origin_boarding_hatch_seal_material_internal.hpp"
#include "origin_boarding_route_checkpoint_world_material03_internal.hpp"
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
namespace {
using namespace apsis_drift;
using Diagnostic = BoardingRouteCheckpointWorldMaterial03Diagnostic;
using Child = BoardingRouteCheckpointUnloadSelf02Diagnostic;
using PointBounds = BoardingPlantedLegPointBounds;
using Limits = detail::BoardingRouteCheckpointWorldLimits;
using Condition = BoardingRouteCheckpointWorldCondition;
using ExtensionAccess = detail::BoardingCheckpointMaterialExtensionAccess;
std::size_t checks{};
int failures{};
void check(bool pass, std::string_view why) {
  ++checks;
  if (!pass) {
    ++failures;
    std::cerr << "FAIL: " << why << '\n';
  }
}
template <class T> auto require(std::expected<T, std::string> r) -> T {
  if (!r)
    throw std::runtime_error("Required WORLD03 API refused: " + r.error());
  return std::move(*r);
}
// Preserve actual inherited fields, not struct padding. Original frame, source
// quad, geometry and numeric-kernel oracles remain mandatory in WORLD01,
// Self02 and extension tests; this test checks the additive consumer boundary.
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

auto limits() -> Limits {
  return detail::boarding_route_checkpoint_world_material03_limits();
}
void denied(const Diagnostic& d) {
  check(!d.route_qualified && !d.actor_qualified && !d.seat_qualified &&
            !d.save_qualified && !d.first_flight_qualified &&
            !d.free_foot_swing_qualified && !d.dynamics_qualified &&
            !d.strength_qualified && !d.friction_qualified,
        "WORLD03 never grants route/actor/seat/save/flight/dynamics authority");
}
void print(std::string_view label, const Diagnostic& d) {
  const auto& p = d.result.world;
  std::cout << std::setprecision(17) << label << " complete=" << p.complete
            << " arithmetic=" << p.arithmetic_supported
            << " child=" << (d.result.child && d.result.child->complete)
            << " cells=" << (d.result.child ? d.result.child->cells.size() : 0)
            << " domain=" << p.domain_complete
            << " material=" << p.material_exclusion
            << " halo=" << p.halo_surface_exclusion
            << " output=" << p.output_capacity_bytes << " work=";
  for (const auto& c : caps)
    std::cout << p.work.*c.work << ',';
  std::cout << p.work.collapsed_triangles << ','
            << p.work.sole_triangle_exclusions << ','
            << p.work.sole_volume_exclusions;
  std::cout << " boundary_work=" << d.boundary_work.witness_attempts << ","
            << d.boundary_work.signed_support_calls << ","
            << d.boundary_work.width_attempts;
  if (p.first_refusal) {
    const auto& r = *p.first_refusal;
    std::cout << " condition=" << static_cast<unsigned>(r.condition)
              << " object=" << r.source_object << " interval=" << r.global_first
              << ',' << r.global_last << " phase=" << r.phase_index;
    if (r.source) std::cout << " source=" << *r.source;
    if (r.part) std::cout << " part=" << *r.part;
    if (r.cell) std::cout << " cell=" << *r.cell;
    if (r.triangle) std::cout << " triangle=" << *r.triangle;
    if (r.source_key)
      std::cout << " key=" << static_cast<unsigned>(r.source_key->buffer) << ','
                << r.source_key->group << ',' << r.source_key->triangle;
  }
  if (d.seal_segment) std::cout << " seal_segment=" << *d.seal_segment;
  std::cout << '\n' << std::flush;
}
void accounting(const Diagnostic& d, const Limits& l = limits()) {
  denied(d);
  check(d.boundary_work.witness_attempts <= 15360 &&
            d.boundary_work.signed_support_calls <= 30720 &&
            d.boundary_work.width_attempts <= 15360 &&
            d.boundary_work.signed_support_calls % 2 == 0,
        "Actual per-cell boundary witness/support/width work remains bounded "
        "and support calls atomic");
  const auto& p = d.result.world;
  const auto& w = p.work;
  // Attribution names the failed genuine obligation, not an executed kernel.
  if (d.seal_segment) {
    check(*d.seal_segment < 8 && !p.complete && p.first_refusal &&
              p.first_refusal->source == 1441 &&
              p.first_refusal->part.has_value() &&
              p.first_refusal->cell.has_value(),
          "Seal ordinal associates only an actual failed source1441 cell "
          "obligation");
    if (p.first_refusal) {
      const auto cause = p.first_refusal->condition;
      check(cause == Condition::pair_capacity ||
                cause == Condition::proxy_capacity ||
                cause == Condition::axis_capacity ||
                cause == Condition::preparation_capacity ||
                cause == Condition::unsupported_arithmetic ||
                cause == Condition::enclosure_unresolved,
            "Seal ordinal is absent from unrelated source/domain/child/HALO "
            "refusals");
    }
  }
  if (p.complete || (p.first_refusal && p.first_refusal->source != 1441))
    check(!d.seal_segment, "Successful or unrelated outcomes never inherit the "
                           "last successful seal segment");
  if (p.first_refusal && p.first_refusal->source == 1441 &&
      p.first_refusal->condition == Condition::enclosure_unresolved)
    check(d.seal_segment.has_value(),
          "Refused genuine seal volume retains its exact segment obligation");
  check(
      p.output_capacity_bytes <= l.output_bytes,
      "Actual additive owned output remains below its lowered allocation cap");
  for (const auto& c : caps)
    check(w.*c.work <= l.*c.limit, "Each actual WORLD03 operation respects its "
                                   "separately registered bound");
  check(w.collapsed_triangles <= w.material_triangle_visits &&
            w.direction_entries_prepared % 16 == 0 &&
            w.sole_vertex_guards <= 192,
        "Full source visits, actual prepared lists and once-only original boot "
        "guards keep their meanings");
  check(detail::BoardingHatchSealMaterialAccess::valid(d.seal, d.result.source),
        "Retained seal stays bound to the same original material capability");
  check(ExtensionAccess::valid(d.extension, d.result.source),
        "Retained extension remains bound to the same original material "
        "capability");
  check(p.source.original_objects == 1746 && p.source.effective_objects == 1751,
        "WORLD03 source1 summary remains original roster rather than "
        "pretending added meshes are added objects");
  if (d.result.child) {
    const auto& child = *d.result.child;
    check(w.union_proxy_preparations <= 15 * child.cells.size() &&
              w.domain_union_checks <= 15 &&
              w.domain_cell_checks <= 15 * child.cells.size(),
          "WORLD03 consumes only the same retained child cover for union and "
          "domain checks");
    check(w.proxy_preparations <=
              w.union_proxy_preparations + w.domain_cell_checks +
                  w.refined_envelope_pairs + w.refined_triangle_pairs +
                  w.refined_enclosure_relations,
          "Actual proxy work is charged by retained obligations without a "
          "second cover");
    if (p.first_refusal && p.first_refusal->cell) {
      const auto& r = *p.first_refusal;
      check(*r.cell < child.cells.size(),
            "Refusal owns an original accepted child cell index");
      if (*r.cell < child.cells.size()) {
        const auto& c = child.cells[*r.cell];
        check(
            r.global_first == c.global_first &&
                r.global_last == c.global_last &&
                r.phase_index == c.phase_index,
            "Source refusal retains exact original closed interval and phase");
      }
    }
  }
  if (p.complete) {
    check(
        d.result.child && d.result.child->complete && !p.first_refusal &&
            p.bindings_complete && p.source_complete &&
            p.arithmetic_supported && p.domain_complete &&
            p.material_exclusion && p.halo_surface_exclusion,
        "Completion requires all unchanged child/domain/material/HALO guards");
    check(w.roster_entries == 1759 && w.effective_sources == 1751 &&
              w.union_envelope_pairs == 26265 && w.halo_metadata_visits == 75 &&
              w.halo_triangle_visits == 8100,
          "Complete consumer visits original effective roster and HALO once");
    for (const auto& part : p.parts)
      check(part.complete && part.domain_complete &&
                part.original_world_identity &&
                part.material_sources_closed == 1751 && d.result.child &&
                part.material_cells_closed ==
                    1751 * d.result.child->cells.size() &&
                part.halo_triangles_closed == 8100,
            "Every original body part closes all effective source and halo "
            "obligations");
  } else
    check(p.first_refusal.has_value(), "Unknown incomplete outcome retains an "
                                       "explicit source or resource refusal");
  if (p.first_refusal && p.first_refusal->source) {
    const auto& r = *p.first_refusal;
    check(r.source_object == d.result.source.source_name(*r.source),
          "Refusal source name stays owned by immutable original source");
    if (r.condition == Condition::missing_relation)
      check(*r.source != 1436 && *r.source != 1574 && *r.source != 1441,
            "Authentically admitted extension supplies exactly its two "
            "formerly missing material relations");
    if (r.source_key)
      check(r.condition != Condition::missing_relation,
            "Native crop occurrence is not treated as missing full-mesh "
            "material permission");
  }
}
void same_world(const Diagnostic& a, const Diagnostic& b) {
  const auto& x = a.result.world;
  const auto& y = b.result.world;
  check(a.seal_segment == b.seal_segment,
        "Canonical reverse preserves failed seal obligation attribution");
  check(x.complete == y.complete &&
            x.first_refusal.has_value() == y.first_refusal.has_value(),
        "Forward/reverse preserve canonical WORLD03 outcome");
  for (const auto& c : caps)
    check(x.work.*c.work == y.work.*c.work,
          "Forward/reverse preserve actual canonical operation counts");
  for (std::size_t i = 0; i < 15; ++i)
    check(x.parts[i].trajectory_union.lower ==
                  y.parts[i].trajectory_union.lower &&
              x.parts[i].trajectory_union.upper ==
                  y.parts[i].trajectory_union.upper,
          "Reverse consumes the same immutable full-motion union");
  if (x.first_refusal && y.first_refusal) {
    const auto& p = *x.first_refusal;
    const auto& q = *y.first_refusal;
    check(p.condition == q.condition && p.part == q.part &&
              p.source == q.source && p.cell == q.cell &&
              p.triangle == q.triangle && p.source_object == q.source_object &&
              p.source_key == q.source_key && p.relation == q.relation &&
              p.global_first == q.global_first &&
              p.global_last == q.global_last && p.phase_index == q.phase_index,
          "Canonical first refusal retains all source/cell/key fields on "
          "reverse request");
  }
}
void invalid_controls(
    const NativeCraftBinding& binding, const OriginBoardingBootSupport& boot,
    const OriginBoardingInitialMaterial& base,
    const OriginBoardingCheckpointMaterialExtension& extension,
    const OriginBoardingHatchSealMaterial& seal) {
  for (double v : std::array{-1., std::nextafter(1., 2.),
                             std::numeric_limits<double>::quiet_NaN(),
                             std::numeric_limits<double>::infinity(),
                             -std::numeric_limits<double>::infinity()})
    check(!assess_origin_boarding_route_checkpoint_world_material03(
              boot, base, extension, seal, v, 1) &&
              !assess_origin_boarding_route_checkpoint_world_material03(
                  boot, base, extension, seal, 0, v),
          "Invalid global clocks refuse before consumer authority");
  auto empty_extension = extension;
  auto retained = std::move(empty_extension);
  // NOLINTBEGIN(bugprone-use-after-move) -- Empty moved-from extension must not
  // authorize the additive consumer.
  check(!assess_origin_boarding_route_checkpoint_world_material03(
            boot, base, empty_extension, seal),
        "Moved extension refuses before original child work");
  // NOLINTEND(bugprone-use-after-move) -- End empty extension contract.
  check(ExtensionAccess::valid(retained, base),
        "Retained extension survives moved-from control");
  auto empty_base = base;
  auto retained_base = std::move(empty_base);
  // NOLINTBEGIN(bugprone-use-after-move) -- Empty moved-from original material
  // invalidates extension identity.
  check(!assess_origin_boarding_route_checkpoint_world_material03(
            boot, empty_base, extension, seal),
        "Moved original source cannot authorize extended consumer");
  // NOLINTEND(bugprone-use-after-move) -- End empty base contract.
  check(ExtensionAccess::valid(extension, retained_base),
        "Copied immutable source keeps extension binding identity");
  const auto different_base = make_origin_boarding_initial_material(binding);
  check(different_base.has_value(),
        "Independent old material still admits unchanged original source");
  if (different_base)
    check(!ExtensionAccess::valid(extension, *different_base) &&
              !assess_origin_boarding_route_checkpoint_world_material03(
                  boot, *different_base, extension, seal),
          "Equal geometry in a separately issued original capability cannot "
          "spoof same-base extension identity");
  auto empty_boot = boot;
  auto retained_boot = std::move(empty_boot);
  (void)retained_boot;
  // NOLINTBEGIN(bugprone-use-after-move) -- Empty boot capability must fail
  // before WORLD work.
  check(!assess_origin_boarding_route_checkpoint_world_material03(
            empty_boot, base, extension, seal),
        "Moved original boot cannot supply source/sole authority");
  // NOLINTEND(bugprone-use-after-move) -- End empty boot contract.
  auto zero = limits();
  zero.output_bytes = 0;
  const auto output =
      require(detail::boarding_route_checkpoint_world_material03_bounded(
          boot, base, extension, seal, 0, 1, zero));
  check(!output.result.child && !output.result.world.complete &&
            output.result.world.first_refusal &&
            output.result.world.first_refusal->condition ==
                Condition::output_capacity &&
            output.result.world.work.proxy_preparations == 0,
        "Zero additive owned output cap stops before original child allocation "
        "or proxy work");
  auto excessive = limits();
  ++excessive.base_enclosure_relations;
  check(!detail::boarding_route_checkpoint_world_material03_bounded(
            boot, base, extension, seal, 0, 0, excessive),
        "Private consumer cannot raise registered source-derived primitive "
        "count");
  const auto rounding = std::fegetround();
  for (int mode : std::array{FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO}) {
    if (std::fesetround(mode) != 0) continue;
    const auto unsafe =
        assess_origin_boarding_route_checkpoint_world_material03(
            boot, base, extension, seal, 0, 0);
    check(!unsafe || (!unsafe->result.world.complete &&
                      !unsafe->result.world.arithmetic_supported),
          "Existing genuine source handles cannot override an unsafe consumer "
          "arithmetic environment");
    if (unsafe) denied(*unsafe);
  }
  check(std::fesetround(rounding) == 0,
        "Restore rounding before first genuine WORLD03 observation");
}
void budgets(const OriginBoardingBootSupport& boot,
             const OriginBoardingInitialMaterial& base,
             const OriginBoardingCheckpointMaterialExtension& extension,
             const OriginBoardingHatchSealMaterial& seal,
             const Diagnostic& observed, double first, double last,
             bool seal_stages = false) {
  for (const auto& c : caps) {
    if (seal_stages && c.limit != &Limits::base_enclosure_relations &&
        c.limit != &Limits::refined_enclosure_relations &&
        c.limit != &Limits::direction_entries_prepared)
      continue;
    const auto used = observed.result.world.work.*c.work;
    if (used == 0) continue;
    auto l = limits();
    l.*c.limit = used;
    const auto exact =
        require(detail::boarding_route_checkpoint_world_material03_bounded(
            boot, base, extension, seal, first, last, l));
    accounting(exact, l);
    const bool eager =
        c.conservative && exact.result.world.first_refusal &&
        exact.result.world.first_refusal->condition == c.condition;
    check(eager ||
              (exact.result.world.complete == observed.result.world.complete &&
               exact.result.world.first_refusal.has_value() ==
                   observed.result.world.first_refusal.has_value()),
          "Exact executed budget preserves observed outcome except genuine "
          "unchanged eager-list guard");
    if (!eager && exact.result.world.first_refusal &&
        observed.result.world.first_refusal)
      check(exact.result.world.first_refusal->condition ==
                    observed.result.world.first_refusal->condition &&
                exact.result.world.first_refusal->source ==
                    observed.result.world.first_refusal->source &&
                exact.result.world.first_refusal->cell ==
                    observed.result.world.first_refusal->cell,
            "Exact reached budget preserves the observed source/cell refusal");
    if (!eager)
      check(exact.seal_segment == observed.seal_segment,
            "Exact actual budget keeps the associated failed seal obligation");
    --(l.*c.limit);
    const auto less =
        require(detail::boarding_route_checkpoint_world_material03_bounded(
            boot, base, extension, seal, first, last, l));
    accounting(less, l);
    check(!less.result.world.complete && less.result.world.first_refusal &&
              less.result.world.first_refusal->condition == c.condition,
          "One-less actually used WORLD03 operation reaches its exact capacity "
          "refusal");
    l = limits();
    l.*c.limit = 0;
    const auto zero =
        require(detail::boarding_route_checkpoint_world_material03_bounded(
            boot, base, extension, seal, first, last, l));
    accounting(zero, l);
    check(!zero.result.world.complete && zero.result.world.first_refusal &&
              zero.result.world.first_refusal->condition == c.condition &&
              zero.result.world.work.*c.work == 0,
          "Reached zero WORLD03 cap stops before the first operation rather "
          "than a masked source refusal");
  }
}

void boundary_budgets(
    const OriginBoardingBootSupport& boot,
    const OriginBoardingInitialMaterial& base,
    const OriginBoardingCheckpointMaterialExtension& extension,
    const OriginBoardingHatchSealMaterial& seal, const Diagnostic& observed,
    double first, double last) {
  const auto& w = observed.boundary_work;
  if (!observed.result.child) return;
  const std::array<std::uint64_t, 3> used{
      w.witness_attempts, w.signed_support_calls, w.width_attempts};
  for (std::size_t which = 0; which < 3; ++which) {
    if (used[which] == 0) continue;
    for (int mode = 0; mode < 3; ++mode) {
      detail::BoardingRouteCheckpointBoundaryLimits l{};
      const auto n = mode == 0 ? 0 : mode == 1 ? used[which] : used[which] - 1;
      if (which == 0) l.witness_attempts = n;
      if (which == 1) l.signed_support_calls = n;
      if (which == 2) l.width_attempts = n;
      const auto r =
          require(detail::boarding_route_checkpoint_world_material03_bounded(
              boot, base, extension, seal, first, last, limits(), l));
      check(
          r.boundary_work.witness_attempts <= l.witness_attempts &&
              r.boundary_work.signed_support_calls <= l.signed_support_calls &&
              r.boundary_work.width_attempts <= l.width_attempts,
          "Actual whole-cover exterior work obeys lowered independent budgets");
      if (mode == 1)
        check(r.result.world.complete == observed.result.world.complete &&
                  r.result.world.first_refusal.has_value() ==
                      observed.result.world.first_refusal.has_value() &&
                  (!r.result.world.first_refusal ||
                   r.result.world.first_refusal->condition ==
                       observed.result.world.first_refusal->condition),
              "Exact actual full-cover witness budgets preserve first observed "
              "WORLD outcome");
      else
        check(!r.result.world.complete && r.result.world.first_refusal &&
                  r.result.world.first_refusal->condition ==
                      (which == 0   ? Condition::boundary_witness_capacity
                       : which == 1 ? Condition::boundary_support_capacity
                                    : Condition::boundary_width_capacity),
              "Zero/one-less actually reached full-cover witness stage refuses "
              "with precise independent capacity");
    }
  }
}

using Solid = detail::BoardingSourceEndpointSurfaceCheckpointSolidBounds;
using Shape = detail::BoardingSourceEndpointSurfaceCheckpointShape;
auto exact_point(RigidVector3 p) -> PointBounds {
  return {p, p};
}
auto small_box(RigidVector3 p, double h = .01) -> Solid {
  return {exact_point(p), exact_point(p), {h, h, h}, 0, Shape::box};
}
void volume_controls() {
  const auto first = exact_point({-1, 0, 0}), last = exact_point({1, 0, 0});
  const auto verify = [&](const Solid& b, PointBounds a, PointBounds z,
                          double r) {
    const auto e =
        require(detail::initial_material_capsule_math(b, 0, a, z, r));
    check(!decltype(e)::source_qualified && !decltype(e)::material_qualified &&
              !decltype(e)::actor_qualified,
          "Raw filled-volume fixture never grants source/material/actor "
          "authority");
    return e;
  };
  const auto inside = verify(small_box({0, 0, 0}), first, last, .125);
  check(inside.arithmetic_supported && !inside.certified,
        "Body wholly inside capsule material refuses despite clear boundary "
        "endpoints");
  const auto endcap = verify(small_box({1.0625, 0, 0}), first, last, .125);
  check(endcap.arithmetic_supported && !endcap.certified,
        "Filled spherical endcap cannot be discarded as a curve surface "
        "endpoint");
  const auto outside = verify(small_box({0, .25, 0}), first, last, .125);
  check(outside.arithmetic_supported && outside.certified,
        "Strict positive full-volume separation certifies arithmetic only");
  const std::array<RigidVector3, 9> ring{{{-1, 0, -1},
                                          {0, 0, -1},
                                          {1, 0, -1},
                                          {1, 0, 0},
                                          {1, 0, 1},
                                          {0, 0, 1},
                                          {-1, 0, 1},
                                          {-1, 0, 0},
                                          {-1, 0, -1}}};
  for (std::size_t i = 0; i < 8; ++i) {
    const auto gap = verify(small_box({0, 0, 0}), exact_point(ring[i]),
                            exact_point(ring[i + 1]), .125);
    check(gap.arithmetic_supported && gap.certified,
          "All eight closed segment obligations preserve the genuine loop "
          "aperture");
  }
  const auto tangent = verify(small_box({0, .25, 0}, .125), first, last, .125);
  check(tangent.arithmetic_supported && !tangent.certified,
        "Exact full-volume tangency cannot grant strict seal exclusion");
  const auto zero = require(detail::initial_material_capsule_math(
      small_box({0, .25, 0}), 0, first, last, .125, 0));
  check(!zero.certified && zero.axes_examined == 0,
        "Zero actual capsule axis budget stops before work");
  check(!detail::initial_material_capsule_math(small_box({0, .25, 0}), 0, first,
                                               last, .125, 17),
        "Raw capsule fixture cannot raise the original sixteen-axis ceiling");
}
void observed_outcome(const Diagnostic& d, std::size_t request) {
  const auto& p = d.result.world;
  constexpr std::array<std::size_t, 8> cells{163, 163, 85, 9, 69, 84, 1, 1};
  constexpr std::array<std::array<std::uint64_t, 24>, 8> work{
      {std::array<std::uint64_t, 24>{
           1590,   1582, 23728, 2445,    15,  0,   20992, 7473,
           352622, 0,    0,     5246877, 0,   90,  11074, 15906,
           171968, 192,  0,     0,       326, 154, 308,   0},
       std::array<std::uint64_t, 24>{
           1590,   1582, 23728, 2445,    15,  0,   20992, 7473,
           352622, 0,    0,     5246877, 0,   90,  11074, 15906,
           171968, 192,  0,     0,       326, 154, 308,   0},
       std::array<std::uint64_t, 24>{1759, 1751, 26265,  1275, 15,    0,
                                     9220, 2635, 346923, 75,   8100,  5325345,
                                     0,    66,   5310,   7226, 82240, 192,
                                     0,    0,    170,    0,    0,     0},
       std::array<std::uint64_t, 24>{1759, 1751, 26265,  135, 15,   0,
                                     1035, 306,  352110, 75,  8100, 5340906,
                                     0,    66,   594,    843, 9216, 192,
                                     0,    0,    18,     0,   0,    0},
       std::array<std::uint64_t, 24>{1590, 1582, 23728,  1035, 15,    0,
                                     9354, 3149, 352622, 0,    0,     5246877,
                                     0,    90,   5170,   7837, 80512, 192,
                                     0,    0,    138,    154,  308,   0},
       std::array<std::uint64_t, 24>{1759, 1751, 26265,  1260, 15,    0,
                                     9886, 3192, 352622, 75,   8100,  5347629,
                                     0,    66,   5434,   7677, 84256, 192,
                                     0,    0,    168,    31,   62,    0},
       std::array<std::uint64_t, 24>{
           1759, 1751, 26265, 15, 15,  0,   106, 30, 346923, 75, 8100, 5325345,
           0,    61,   61,    78, 944, 192, 0,   0,  2,      0,  0,    0},
       std::array<std::uint64_t, 24>{
           1590, 1582, 23728, 15,  15,   0,   151, 46, 352622, 0, 0, 5246877,
           0,    90,   90,    139, 1408, 192, 0,   0,  2,      3, 6, 0}}};
  constexpr std::array<bool, 8> complete{false, false, true, true,
                                         false, true,  true, false};
  check(request < cells.size(),
        "Observed request has a registered FIRST record");
  if (request >= cells.size()) return;
  check(d.result.child && d.result.child->complete &&
            d.result.child->cells.size() == cells[request] &&
            p.world_version == 3 && p.bindings_complete && p.source_complete &&
            p.arithmetic_supported && p.domain_complete,
        "Frozen FIRST retains complete original child, source and "
        "arithmetic/domain prerequisites");
  check(p.complete == complete[request] &&
            p.material_exclusion == complete[request] &&
            p.halo_surface_exclusion == complete[request] &&
            p.first_refusal.has_value() == !complete[request],
        "Frozen FIRST request retains its exact material/HALO completion or "
        "honest refusal");
  std::array<std::uint64_t, 24> actual{};
  for (std::size_t i = 0; i < caps.size(); ++i)
    actual[i] = p.work.*caps[i].work;
  actual[18] = p.work.collapsed_triangles;
  actual[19] = p.work.sole_triangle_exclusions;
  actual[20] = p.work.sole_volume_exclusions;
  actual[21] = d.boundary_work.witness_attempts;
  actual[22] = d.boundary_work.signed_support_calls;
  actual[23] = d.boundary_work.width_attempts;
  check(
      actual == work[request],
      "Frozen FIRST retains every observed actual WORLD and boundary counter");
  check(p.output_capacity_bytes == (request >= 6 ? 49616u : 10418744u),
        "Frozen FIRST retains actual owned output accounting");
  check(!d.seal_segment, "Every normal FIRST request leaves seal attribution "
                         "empty on complete/unrelated outcomes");
  if (!complete[request] && p.first_refusal) {
    const auto& r = *p.first_refusal;
    check(r.condition == Condition::missing_relation && r.source == 1589 &&
              r.part == 12 &&
              r.source_object == "WF02 | separation load ring" &&
              r.phase_index == 2 &&
              r.cell == (request == 4   ? 43u
                         : request == 7 ? 0u
                                        : 137u) &&
              r.global_first == (request == 7 ? 1. : .8203125) &&
              r.global_last == (request == 7 ? 1. : .828125) && !r.triangle &&
              !r.source_key,
          "Frozen FIRST retains the exact original separation load ring "
          "refusal and closed child interval");
  }
}
void observe(const OriginBoardingBootSupport& boot,
             const OriginBoardingInitialMaterial& base,
             const OriginBoardingCheckpointMaterialExtension& extension,
             const OriginBoardingHatchSealMaterial& seal) {
  auto r = assess_origin_boarding_route_checkpoint_world_material03(
      boot, base, extension, seal);
  if (!r) {
    std::cout
        << "FIRST_PUBLIC_CHECKPOINT_WORLD_MATERIAL03 api_accepted=0 error="
        << r.error() << '\n'
        << std::flush;
    throw std::runtime_error(r.error());
  }
  auto first = std::move(*r);
  print("FIRST_PUBLIC_CHECKPOINT_WORLD_MATERIAL03", first);
  // Preserve FIRST logging before mandatory regressions from frozen4e981a1.
  observed_outcome(first, 0);
  accounting(first);
  const auto original =
      require(assess_origin_boarding_route_checkpoint_unload_self02(boot));
  check(first.result.child &&
            snapshot(*first.result.child) == snapshot(original),
        "WORLD03 retains the unchanged sole original child and all its fields");
  boundary_budgets(boot, base, extension, seal, first, 0, 1);
  budgets(boot, base, extension, seal, first, 0, 1, true);
  const std::array<std::pair<double, double>, 8> requests{{{0, 1},
                                                           {1, 0},
                                                           {0, .25},
                                                           {.25, .5},
                                                           {.5, 1},
                                                           {.125, .75},
                                                           {0, 0},
                                                           {1, 1}}};
  for (std::size_t i = 1; i < requests.size(); ++i) {
    const auto [a, b] = requests[i];
    auto result = assess_origin_boarding_route_checkpoint_world_material03(
        boot, base, extension, seal, a, b);
    if (!result) {
      std::cout << "WORLD03_REQUEST_" << i
                << " api_accepted=0 error=" << result.error() << '\n'
                << std::flush;
      check(
          false,
          "Valid source-bound request retains diagnostic instead of API error");
      continue;
    }
    print("WORLD03_REQUEST_" + std::to_string(i), *result);
    observed_outcome(*result, i);
    accounting(*result);
    const auto child = require(
        assess_origin_boarding_route_checkpoint_unload_self02(boot, a, b));
    check(result->result.child &&
              snapshot(*result->result.child) == snapshot(child),
          "WORLD03 shares the exact original request cover, geometry, "
          "pressure, owner and work fields");
    if (i == 1) same_world(first, *result);
    if (i == 6) budgets(boot, base, extension, seal, *result, a, b);
  }
  const auto legacy =
      require(assess_origin_boarding_route_checkpoint_world_material02(
          boot, base, extension));
  check(!legacy.result.world.complete && legacy.result.world.first_refusal &&
            legacy.result.world.first_refusal->condition ==
                Condition::missing_relation &&
            legacy.result.world.first_refusal->source == 1441 &&
            legacy.result.world.first_refusal->part == 12 &&
            legacy.result.world.first_refusal->cell == 133 &&
            legacy.result.world.first_refusal->global_first == .79296875 &&
            legacy.result.world.first_refusal->global_last == .796875,
        "Existing WORLD02 retains the named seal material refusal, not a "
        "collision certificate");
  auto moved = std::move(first);
  check(moved.result.child &&
            ExtensionAccess::valid(moved.extension, moved.result.source) &&
            detail::BoardingHatchSealMaterialAccess::valid(moved.seal,
                                                           moved.result.source),
        "Moved WORLD03 report retains original child and immutable material "
        "lifetimes");
  if (moved.result.world.first_refusal &&
      moved.result.world.first_refusal->source)
    check(moved.result.world.first_refusal->source_object ==
              moved.result.source.source_name(
                  *moved.result.world.first_refusal->source),
          "Moved report owns actual source name lifetime");
}
} // namespace
int main() {
  try {
    volume_controls();
    const auto l = limits();
    check(l.base_enclosure_relations == 360 &&
              l.refined_enclosure_relations == 368640 &&
              l.direction_entries_prepared == 20217856,
          "Seal adds no capacity increase to unchanged shared "
          "direction/enclosure ceilings");
    const auto binding_result = make_native_starting_assembly_binding(
        NativeStartingAssemblySelection{});
    if (!binding_result) throw std::runtime_error(binding_result.error());
    const auto& binding = *binding_result;
    const auto base_result = make_origin_boarding_initial_material(binding);
    if (!base_result) throw std::runtime_error(base_result.error());
    const auto& base = *base_result;
    const auto boot_result = make_origin_boarding_boot_support(binding);
    if (!boot_result) throw std::runtime_error(boot_result.error());
    const auto& boot = *boot_result;
    detail::BoardingCheckpointMaterialBoundaryEvidence frame_evidence;
    const auto extension =
        ExtensionAccess::make_boundary03(binding, base, {}, &frame_evidence);
    if (!extension) throw std::runtime_error(extension.error());
    detail::BoardingHatchSealMaterialConstructorMath seal_evidence;
    const auto seal = detail::BoardingHatchSealMaterialAccess::make(
        binding, base, {}, &seal_evidence);
    std::cout << "WORLD03_SEAL_ADMISSION accepted=" << seal.has_value()
              << " condition=" << static_cast<unsigned>(seal_evidence.condition)
              << " raw=" << seal_evidence.work.raw_inclusions
              << " grid=" << seal_evidence.work.quantized_inclusions;
    if (!seal) std::cout << " error=" << seal.error();
    if (seal_evidence.triangle)
      std::cout << " triangle=" << *seal_evidence.triangle;
    if (seal_evidence.strip) std::cout << " strip=" << *seal_evidence.strip;
    if (seal_evidence.vertex) std::cout << " vertex=" << *seal_evidence.vertex;
    std::cout << '\n' << std::flush;
    check(seal.has_value() && seal_evidence.complete &&
              seal_evidence.condition ==
                  detail::BoardingHatchSealMaterialCondition::none &&
              seal_evidence.work.raw_inclusions == 480 &&
              seal_evidence.work.quantized_inclusions == 480,
          "Frozen FIRST WORLD03 remains available through genuine completed "
          "seal admission");
    if (seal) {
      invalid_controls(binding, boot, base, *extension, *seal);
      auto empty = *seal;
      auto retained = std::move(empty);
      // NOLINTBEGIN(bugprone-use-after-move) -- Empty moved-from seal must
      // refuse before WORLD child work.
      check(!assess_origin_boarding_route_checkpoint_world_material03(
                boot, base, *extension, empty),
            "Moved seal cannot authenticate WORLD03 material");
      // NOLINTEND(bugprone-use-after-move) -- End moved-from seal contract.
      check(detail::BoardingHatchSealMaterialAccess::valid(retained, base),
            "Retained seal survives invalid moved-handle control");
      observe(boot, base, *extension, *seal);
    } else {
      check(!seal_evidence.complete &&
                !seal_evidence.work.constructors_complete,
            "Unknown issuer refusal remains unavailable without inventing "
            "source authority");
      std::cout << "WORLD03_UNAVAILABLE clearance_observed=0\n" << std::flush;
    }
  } catch (const std::exception& e) {
    ++failures;
    std::cerr << "Exception: " << e.what() << '\n';
  }
  std::cout << "WORLD03 checks=" << checks << " failures=" << failures << '\n';
  return failures == 0 ? 0 : 1;
}
