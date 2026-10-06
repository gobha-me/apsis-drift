#include "apsis_drift/origin_boarding_route_checkpoint_world_material02.hpp"
#include "origin_boarding_checkpoint_material_extension_internal.hpp"
#include "origin_boarding_route_checkpoint_world_material02_internal.hpp"
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
using Diagnostic = BoardingRouteCheckpointWorldMaterial02Diagnostic;
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
    throw std::runtime_error("Required WORLD02 API refused: " + r.error());
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
  return detail::boarding_route_checkpoint_world_material02_limits();
}
void denied(const Diagnostic& d) {
  check(!d.route_qualified && !d.actor_qualified && !d.seat_qualified &&
            !d.save_qualified && !d.first_flight_qualified &&
            !d.free_foot_swing_qualified && !d.dynamics_qualified &&
            !d.strength_qualified && !d.friction_qualified,
        "WORLD02 never grants route/actor/seat/save/flight/dynamics authority");
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
  std::cout << '\n' << std::flush;
}
void accounting(const Diagnostic& d, const Limits& l = limits()) {
  denied(d);
  const auto& p = d.result.world;
  const auto& w = p.work;
  check(
      p.output_capacity_bytes <= l.output_bytes,
      "Actual additive owned output remains below its lowered allocation cap");
  for (const auto& c : caps)
    check(w.*c.work <= l.*c.limit, "Each actual WORLD02 operation respects its "
                                   "separately registered bound");
  check(w.collapsed_triangles <= w.material_triangle_visits &&
            w.direction_entries_prepared % 16 == 0 &&
            w.sole_vertex_guards <= 192,
        "Full source visits, actual prepared lists and once-only original boot "
        "guards keep their meanings");
  check(ExtensionAccess::valid(d.extension, d.result.source),
        "Retained extension remains bound to the same original material "
        "capability");
  check(p.source.original_objects == 1746 && p.source.effective_objects == 1751,
        "WORLD02 source1 summary remains original roster rather than "
        "pretending added meshes are added objects");
  if (d.result.child) {
    const auto& child = *d.result.child;
    check(w.union_proxy_preparations <= 15 * child.cells.size() &&
              w.domain_union_checks <= 15 &&
              w.domain_cell_checks <= 15 * child.cells.size(),
          "WORLD02 consumes only the same retained child cover for union and "
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
      check(*r.source != 1436 && *r.source != 1574,
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
  check(x.complete == y.complete &&
            x.first_refusal.has_value() == y.first_refusal.has_value(),
        "Forward/reverse preserve canonical WORLD02 outcome");
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
    const OriginBoardingCheckpointMaterialExtension& extension) {
  for (double v : std::array{-1., std::nextafter(1., 2.),
                             std::numeric_limits<double>::quiet_NaN(),
                             std::numeric_limits<double>::infinity(),
                             -std::numeric_limits<double>::infinity()})
    check(!assess_origin_boarding_route_checkpoint_world_material02(
              boot, base, extension, v, 1) &&
              !assess_origin_boarding_route_checkpoint_world_material02(
                  boot, base, extension, 0, v),
          "Invalid global clocks refuse before consumer authority");
  auto empty_extension = extension;
  auto retained = std::move(empty_extension);
  // NOLINTBEGIN(bugprone-use-after-move) -- Empty moved-from extension must not
  // authorize the additive consumer.
  check(!assess_origin_boarding_route_checkpoint_world_material02(
            boot, base, empty_extension),
        "Moved extension refuses before original child work");
  // NOLINTEND(bugprone-use-after-move) -- End empty extension contract.
  check(ExtensionAccess::valid(retained, base),
        "Retained extension survives moved-from control");
  auto empty_base = base;
  auto retained_base = std::move(empty_base);
  // NOLINTBEGIN(bugprone-use-after-move) -- Empty moved-from original material
  // invalidates extension identity.
  check(!assess_origin_boarding_route_checkpoint_world_material02(
            boot, empty_base, extension),
        "Moved original source cannot authorize extended consumer");
  // NOLINTEND(bugprone-use-after-move) -- End empty base contract.
  check(ExtensionAccess::valid(extension, retained_base),
        "Copied immutable source keeps extension binding identity");
  const auto different_base = make_origin_boarding_initial_material(binding);
  check(different_base.has_value(),
        "Independent old material still admits unchanged original source");
  if (different_base)
    check(!ExtensionAccess::valid(extension, *different_base) &&
              !assess_origin_boarding_route_checkpoint_world_material02(
                  boot, *different_base, extension),
          "Equal geometry in a separately issued original capability cannot "
          "spoof same-base extension identity");
  auto empty_boot = boot;
  auto retained_boot = std::move(empty_boot);
  (void)retained_boot;
  // NOLINTBEGIN(bugprone-use-after-move) -- Empty boot capability must fail
  // before WORLD work.
  check(!assess_origin_boarding_route_checkpoint_world_material02(
            empty_boot, base, extension),
        "Moved original boot cannot supply source/sole authority");
  // NOLINTEND(bugprone-use-after-move) -- End empty boot contract.
  auto zero = limits();
  zero.output_bytes = 0;
  const auto output =
      require(detail::boarding_route_checkpoint_world_material02_bounded(
          boot, base, extension, 0, 1, zero));
  check(!output.result.child && !output.result.world.complete &&
            output.result.world.first_refusal &&
            output.result.world.first_refusal->condition ==
                Condition::output_capacity &&
            output.result.world.work.proxy_preparations == 0,
        "Zero additive owned output cap stops before original child allocation "
        "or proxy work");
  auto excessive = limits();
  ++excessive.base_enclosure_relations;
  check(!detail::boarding_route_checkpoint_world_material02_bounded(
            boot, base, extension, 0, 0, excessive),
        "Private consumer cannot raise registered source-derived primitive "
        "count");
  const auto rounding = std::fegetround();
  for (int mode : std::array{FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO}) {
    if (std::fesetround(mode) != 0) continue;
    const auto unsafe =
        assess_origin_boarding_route_checkpoint_world_material02(
            boot, base, extension, 0, 0);
    check(!unsafe || (!unsafe->result.world.complete &&
                      !unsafe->result.world.arithmetic_supported),
          "Existing genuine source handles cannot override an unsafe consumer "
          "arithmetic environment");
    if (unsafe) denied(*unsafe);
  }
  check(std::fesetround(rounding) == 0,
        "Restore rounding before first genuine WORLD02 observation");
}
void budgets(const OriginBoardingBootSupport& boot,
             const OriginBoardingInitialMaterial& base,
             const OriginBoardingCheckpointMaterialExtension& extension,
             const Diagnostic& observed) {
  for (const auto& c : caps) {
    const auto used = observed.result.world.work.*c.work;
    if (used == 0) continue;
    auto l = limits();
    l.*c.limit = used;
    const auto exact =
        require(detail::boarding_route_checkpoint_world_material02_bounded(
            boot, base, extension, 0, 0, l));
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
    --(l.*c.limit);
    const auto less =
        require(detail::boarding_route_checkpoint_world_material02_bounded(
            boot, base, extension, 0, 0, l));
    accounting(less, l);
    check(!less.result.world.complete && less.result.world.first_refusal &&
              less.result.world.first_refusal->condition == c.condition,
          "One-less actually used WORLD02 operation reaches its exact capacity "
          "refusal");
    l = limits();
    l.*c.limit = 0;
    const auto zero =
        require(detail::boarding_route_checkpoint_world_material02_bounded(
            boot, base, extension, 0, 0, l));
    accounting(zero, l);
    check(!zero.result.world.complete && zero.result.world.first_refusal &&
              zero.result.world.first_refusal->condition == c.condition &&
              zero.result.world.work.*c.work == 0,
          "Reached zero WORLD02 cap stops before the first operation rather "
          "than a masked source refusal");
  }
}
void observe(const OriginBoardingBootSupport& boot,
             const OriginBoardingInitialMaterial& base,
             const OriginBoardingCheckpointMaterialExtension& extension) {
  auto first_result = assess_origin_boarding_route_checkpoint_world_material02(
      boot, base, extension);
  if (!first_result) {
    std::cout
        << "FIRST_PUBLIC_CHECKPOINT_WORLD_MATERIAL02 api_accepted=0 error="
        << first_result.error() << '\n'
        << std::flush;
    throw std::runtime_error(first_result.error());
  }
  auto first = std::move(*first_result);
  print("FIRST_PUBLIC_CHECKPOINT_WORLD_MATERIAL02", first);
  // The new consumer outcome is not assumed before its frozen first log.
  accounting(first);
  const auto first_child = require(
      assess_origin_boarding_route_checkpoint_unload_self02(boot, 0, 1));
  check(first.result.child &&
            snapshot(*first.result.child) == snapshot(first_child),
        "Whole additive consumer preserves every original child field");
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
    auto result =
        require(assess_origin_boarding_route_checkpoint_world_material02(
            boot, base, extension, a, b));
    print("WORLD02_REQUEST_" + std::to_string(i), result);
    accounting(result);
    const auto child = require(
        assess_origin_boarding_route_checkpoint_unload_self02(boot, a, b));
    check(result.result.child &&
              snapshot(*result.result.child) == snapshot(child),
          "Additive WORLD consumer preserves every original child "
          "geometry/COM/frames/pressure/owner/certificate/control and work "
          "field");
    if (i == 1) same_world(first, result);
    if (i == 6) budgets(boot, base, extension, result);
  }
  const auto legacy = require(
      assess_origin_boarding_route_checkpoint_world_material(boot, base, 1, 1));
  check(!legacy.world.complete && legacy.world.first_refusal &&
            legacy.world.first_refusal->condition ==
                Condition::missing_relation &&
            legacy.world.first_refusal->source == 1436,
        "Existing WORLD01 remains the original named refusal after explicit "
        "extension admission");
  auto moved = std::move(first);
  check(moved.result.child &&
            ExtensionAccess::valid(moved.extension, moved.result.source),
        "Moved public WORLD02 report keeps child and immutable "
        "source/extension lifetimes");
  if (moved.result.world.first_refusal &&
      moved.result.world.first_refusal->source)
    check(moved.result.world.first_refusal->source_object ==
              moved.result.source.source_name(
                  *moved.result.world.first_refusal->source),
          "Moved report owns retained source-name lifetime without copied "
          "transient strings");
}
} // namespace
int main() {
  try {
    const auto l = limits();
    check(l.base_enclosure_relations == 360 &&
              l.refined_enclosure_relations == 368640 &&
              l.proxy_preparations == 2496512 &&
              l.material_triangle_visits == 354786 &&
              l.triangle_union_pairs == 5443290 &&
              l.halo_triangle_visits == 8100,
          "Source-derived WORLD02 capacities remain exact registration with "
          "inherited original HALO bound");
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
    detail::BoardingCheckpointMaterialExtensionConstructorMath e;
    const auto admission = ExtensionAccess::make(binding, base, {}, &e);
    std::cout << "WORLD02_EXTENSION_ADMISSION accepted="
              << admission.has_value()
              << " condition=" << static_cast<unsigned>(e.condition)
              << " raw=" << e.work.raw_plane_guards
              << " quantized=" << e.work.quantized_plane_guards;
    if (!admission) std::cout << " error=" << admission.error();
    std::cout << '\n' << std::flush;
    check(!admission && !e.complete && !e.work.constructors_complete &&
              e.condition ==
                  detail::BoardingCheckpointMaterialExtensionCondition::
                      raw_containment &&
              e.source == 1436 && e.triangle == 2 && e.sector == 7 &&
              e.vertex == 3 && e.plane == 3 && !e.quantized,
          "Frozen WORLD02 remains unavailable because original frame raw "
          "containment refused; this is no WORLD clearance result");
    if (admission) {
      const auto& extension = *admission;
      invalid_controls(binding, boot, base, extension);
      observe(boot, base, extension);
    } else {
      std::cout << "CHECKPOINT_WORLD_MATERIAL02_UNAVAILABLE "
                   "reason=frame_raw_containment "
                   "source=1436 triangle=2 clearance_observed=0\n"
                << std::flush;
      check(!e.complete && !e.work.constructors_complete,
            "Unavailable constructor is preserved; no fake extension mints "
            "WORLD permission");
    }
    detail::BoardingCheckpointMaterialExtensionConstructorMath union_evidence;
    const auto union_admission =
        ExtensionAccess::make_union02(binding, base, {}, &union_evidence);
    std::cout << "WORLD02_UNION02_EXTENSION_ADMISSION accepted="
              << union_admission.has_value() << " condition="
              << static_cast<unsigned>(union_evidence.condition)
              << " version=" << union_evidence.work.extension_version
              << " pairs=" << union_evidence.adjacent_pair_attempts
              << " raw=" << union_evidence.work.raw_plane_guards
              << " quantized=" << union_evidence.work.quantized_plane_guards;
    if (!union_admission) std::cout << " error=" << union_admission.error();
    std::cout << '\n' << std::flush;
    if (union_admission) {
      const auto& extension = *union_admission;
      check(extension.summary() && extension.summary()->extension_version == 2,
            "Only explicit genuinely admitted Union02 capability starts the "
            "new consumer observation");
      invalid_controls(binding, boot, base, extension);
      observe(boot, base, extension);
    } else {
      std::cout << "CHECKPOINT_WORLD_MATERIAL02_UNAVAILABLE "
                   "reason=union02_constructor clearance_observed=0\n"
                << std::flush;
      check(!union_evidence.complete &&
                !union_evidence.work.constructors_complete,
            "Refused Union02 retains no source handle and no WORLD clearance "
            "observation");
    }
  } catch (const std::exception& e) {
    ++failures;
    std::cerr << "EXCEPTION: " << e.what() << '\n';
  }
  std::cout << checks << " checks, " << failures << " failures\n";
  return failures == 0 ? 0 : 1;
}
