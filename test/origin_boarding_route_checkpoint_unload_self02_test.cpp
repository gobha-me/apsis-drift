#include "apsis_drift/origin_boarding_route_checkpoint_unload_self02.hpp"
#include "origin_boarding_route_checkpoint_unload_internal.hpp"
#include "origin_boarding_route_checkpoint_unload_self02_internal.hpp"
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
  if (!r) throw std::runtime_error("Required self02 test API refused");
  return std::move(*r);
}
auto component(RigidVector3 p, std::size_t i) -> double {
  return i == 0 ? p.x : i == 1 ? p.y : p.z;
}
auto dot(Wide a, Wide b) -> long double {
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
auto add(Wide a, Wide b) -> Wide {
  for (std::size_t i = 0; i < 3; ++i)
    a[i] += b[i];
  return a;
}
auto scale(Wide a, long double n) -> Wide {
  for (auto& v : a)
    v *= n;
  return a;
}
auto contains(Bounds b, long double n) -> bool {
  const auto roundoff = 2e-12L * std::max(1.L, std::abs(n));
  return std::isfinite(b.lower) && std::isfinite(b.upper) &&
         b.lower <= b.upper && b.lower <= n + roundoff &&
         b.upper >= n - roundoff;
}
auto capsule_distance_squared(Wide p, Wide u, long double length)
    -> long double {
  const auto s = std::clamp(dot(p, u), 0.L, length);
  const auto difference = add(p, scale(u, -s));
  return dot(difference, difference);
}
auto in_original_port_pelvis(Wide p) -> bool {
  return std::abs(p[0] - .14L) < .24L && std::abs(p[1]) < .12L &&
         std::abs(p[2]) < .18L;
}
// Synthetic unit axes are independently defined by rational Pythagorean
// triples. Numerical seams receive only component intervals, never authority
// that an arbitrary caller axis/body is nominally unit or admitted.
void theorem_geometry() {
  const Wide clear{7.L / 25, -24.L / 25, 0}, conflict{4.L / 5, -3.L / 5, 0};
  const long double radius = .105L, length = .47285L,
                    limit = kBoardingSelfHipLengthMetres;
  check(std::abs(dot(clear, clear) - 1) <
                8 * std::numeric_limits<long double>::epsilon() &&
            std::abs(dot(conflict, conflict) - 1) <
                8 * std::numeric_limits<long double>::epsilon(),
        "Independent rational fixture axes are genuinely unit for geometric "
        "corroboration");
  const auto old_extent = .14L * clear[0] + .24L * std::abs(clear[0]) +
                          .12L * std::abs(clear[1]) + .18L * std::abs(clear[2]);
  const auto extent =
      limit * clear[1] + radius * std::hypot(clear[0], clear[2]);
  check(old_extent > limit && extent < -.12L,
        "Complement can contain real overlap in unchanged slab when "
        "whole-pelvis support fails");
  // Capsule root hemisphere cannot escape the slab: every root-ball point
  // has t<=r<=Lhip. The distal hemisphere is retained, not shortened away.
  check(radius <= limit && limit < length,
        "Original complete capsule includes both caps under retained finite "
        "slab guard");
  const auto root_tip = scale(clear, -radius),
             distal_tip = scale(clear, length + radius);
  check(
      dot(root_tip, clear) <= limit &&
          capsule_distance_squared(root_tip, clear, length) <=
              radius * radius + 1e-18L,
      "Root hemisphere remains a complete original capsule member inside slab");
  check(dot(distal_tip, clear) > limit &&
            capsule_distance_squared(distal_tip, clear, length) <=
                radius * radius + 1e-18L &&
            distal_tip[1] < -.12L,
        "Original distal cap outside slab is covered by complement bound");
  const Wide upward_transverse{24.L / 25, 7.L / 25, 0};
  const auto cylinder_extreme =
      add(scale(clear, length), scale(upward_transverse, radius));
  check(dot(cylinder_extreme, clear) > limit && cylinder_extreme[1] <= extent &&
            capsule_distance_squared(cylinder_extreme, clear, length) <=
                radius * radius + 1e-18L,
        "Full cylinder transverse-radius extreme also obeys all-capsule "
        "complement bound");
  // Explicit STRICT common interior outside the original slab. This is a
  // real failure control, not a sampled centerline or a tiny tolerance case.
  const Wide transverse{3.L / 5, 4.L / 5, 0};
  const auto witness = add(scale(conflict, .24L), scale(transverse, .08L));
  check(in_original_port_pelvis(witness) && dot(witness, conflict) > limit &&
            capsule_distance_squared(witness, conflict, length) <
                radius * radius,
        "Independent finite capsule/slab/box witness proves true "
        "outside-region strict conflict");
  const auto failed_extent =
      limit * conflict[1] + radius * std::hypot(conflict[0], conflict[2]);
  check(failed_extent > -.12L,
        "True conflict fixture must refuse sufficient complement proof");
}
using Diagnostic = BoardingRouteCheckpointUnloadSelf02Diagnostic;
using Cell = BoardingRouteCheckpointUnloadSelf02Cell;
using Limits = detail::BoardingRouteCheckpointUnloadSelf02Limits;
using Certificate = BoardingRouteCheckpointUnloadSelf02Certificate;
using Condition = BoardingRoutePortUnloadCondition;
using State = detail::BoardingRoutePortUnloadCellResult;
auto enclosed_fraction(long double value) -> Bounds {
  const auto middle = static_cast<double>(value);
  return {std::nextafter(middle, -std::numeric_limits<double>::infinity()),
          std::nextafter(middle, std::numeric_limits<double>::infinity())};
}
void expression_controls() {
  using Math = detail::BoardingRouteCheckpointUnloadSelf02HipMath;
  auto inspect = [](const Math& m) {
    check(!m.unit_qualified && !m.source_qualified && !m.body_qualified &&
              !m.self_qualified && !m.support_qualified && !m.actor_qualified,
          "Raw expression never gives caller unit, capsule, owner, source or "
          "actor authority");
  };
  for (const auto& axis : std::array<Wide, 3>{{{7.L / 25, -24.L / 25, 0},
                                               {5.L / 13, -12.L / 13, 0},
                                               {4.L / 5, -3.L / 5, 0}}}) {
    std::array<Bounds, 3> bounds{enclosed_fraction(axis[0]),
                                 enclosed_fraction(axis[1]), Bounds{0, 0}};
    const auto e =
        require(detail::boarding_route_checkpoint_unload_self02_hip_math(
            bounds, .105, kBoardingSelfHipLengthMetres, -.12));
    inspect(e);
    const auto transverse = std::hypot(axis[0], axis[2]),
               extent = static_cast<long double>(kBoardingSelfHipLengthMetres) *
                            axis[1] +
                        static_cast<long double>(.105) * transverse;
    check(e.arithmetic_supported && e.negative_axis &&
              contains(e.transverse, transverse) &&
              contains(e.extent, extent) &&
              contains(e.limit, static_cast<long double>(-.12)) &&
              contains(e.strict_gap, static_cast<long double>(-.12) - extent),
          "Independent rational unit fixture encloses transverse, extent and "
          "strict gap expression");
    check(e.strict_extent_below_bottom == (extent < -.12L),
          "Theorem expression accepts clear complement and refuses actual "
          "outside-slab conflict");
  }
  const auto vertical =
      require(detail::boarding_route_checkpoint_unload_self02_hip_math(
          {Bounds{0, 0}, Bounds{-1, -1}, Bounds{0, 0}}, .105,
          kBoardingSelfHipLengthMetres, -.12));
  inspect(vertical);
  check(vertical.arithmetic_supported && vertical.transverse.lower == 0 &&
            vertical.transverse.upper == 0 &&
            vertical.strict_extent_below_bottom,
        "Exactly vertical axis permits zero value-only transverse root");
  const auto boundary =
      require(detail::boarding_route_checkpoint_unload_self02_hip_math(
          {Bounds{0, 0}, Bounds{-1, -1}, Bounds{0, 0}}, .105, .12, -.12));
  inspect(boundary);
  check(boundary.arithmetic_supported && !boundary.strict_extent_below_bottom &&
            contains(boundary.strict_gap, 0),
        "Exact closed slab/pelvis boundary refuses strict sufficient proof");
  for (double y : {0., .5}) {
    const auto e =
        require(detail::boarding_route_checkpoint_unload_self02_hip_math(
            {Bounds{0, 0}, Bounds{y, y}, Bounds{0, 0}}, .105,
            kBoardingSelfHipLengthMetres, -.12));
    inspect(e);
    check(!e.negative_axis && !e.strict_extent_below_bottom,
          "Nonnegative axialY cannot establish monotone outside-slab "
          "certificate");
  }
  const auto nonunit =
      require(detail::boarding_route_checkpoint_unload_self02_hip_math(
          {Bounds{0, 0}, Bounds{-.5, -.5}, Bounds{0, 0}}, .1, .5, -.12));
  inspect(nonunit);
  check(nonunit.strict_extent_below_bottom && !nonunit.unit_qualified &&
            !nonunit.self_qualified,
        "Negative caller expression with nonunit axis still grants no "
        "unit/self certificate");
  const auto inf = std::numeric_limits<double>::infinity(),
             nan = std::numeric_limits<double>::quiet_NaN();
  const std::array<Bounds, 3> good{Bounds{0, 0}, Bounds{-1, -1}, Bounds{0, 0}};
  for (double invalid : {nan, inf, -inf}) {
    for (std::size_t axis = 0; axis < 3; ++axis) {
      auto b = good;
      b[axis].lower = invalid;
      check(!detail::boarding_route_checkpoint_unload_self02_hip_math(
                b, .105, kBoardingSelfHipLengthMetres, -.12),
            "Nonfinite raw component refuses before expression proof");
    }
    check(!detail::boarding_route_checkpoint_unload_self02_hip_math(
              good, invalid, kBoardingSelfHipLengthMetres, -.12) &&
              !detail::boarding_route_checkpoint_unload_self02_hip_math(
                  good, .105, invalid, -.12) &&
              !detail::boarding_route_checkpoint_unload_self02_hip_math(
                  good, .105, kBoardingSelfHipLengthMetres, invalid),
          "Nonfinite scalar dimensions cannot enroll theorem");
  }
  auto bad = good;
  bad[0] = {1, -1};
  check(!detail::boarding_route_checkpoint_unload_self02_hip_math(
            bad, .105, kBoardingSelfHipLengthMetres, -.12),
        "Unordered raw component refuses");
  bad = good;
  bad[0] = {0, 1.01};
  check(!detail::boarding_route_checkpoint_unload_self02_hip_math(
            bad, .105, kBoardingSelfHipLengthMetres, -.12),
        "Component outside finite expression family refuses");
  check(!detail::boarding_route_checkpoint_unload_self02_hip_math(
            good, 0, kBoardingSelfHipLengthMetres, -.12) &&
            !detail::boarding_route_checkpoint_unload_self02_hip_math(
                good, .3, kBoardingSelfHipLengthMetres, -.12),
        "Zero radius or radius beyond original slab constraint refuses");
}
auto old_child(const BoardingSourceEndpointLoadDiagnostic& d)
    -> std::vector<std::uint64_t> {
  std::vector<std::uint64_t> out;
  const auto number = [&](double v) {
    out.push_back(std::bit_cast<std::uint64_t>(v));
  };
  const auto bounds = [&](BoardingPlantedLegPointBounds p) {
    for (auto v : std::array{p.lower, p.upper}) {
      number(v.x);
      number(v.y);
      number(v.z);
    }
  };
  const auto& e = d.self.endpoint;
  number(e.common_translation_y_metres);
  number(e.root_z_metres);
  if (e.body) {
    for (auto p : e.body->points)
      bounds(p);
    for (auto p : e.body->mass_points)
      bounds(p);
    bounds(e.body->center_of_mass);
  }
  for (const auto& p : d.self.pairs) {
    out.push_back(static_cast<std::uint64_t>(p.certificate));
    number(p.certificate_gap.lower);
    number(p.certificate_gap.upper);
    number(p.ownership_extent.lower);
    number(p.ownership_extent.upper);
    number(p.ownership_limit.lower);
    number(p.ownership_limit.upper);
    out.push_back(p.certified);
    out.push_back(p.examined_axes);
  }
  for (const auto& p : d.load.pressures) {
    number(p.plane_metres);
    for (auto b : p.pressure_bounds_metres)
      bounds({b, b});
    for (auto key : p.source_keys) {
      out.push_back(static_cast<std::uint64_t>(key.buffer));
      out.push_back(key.group);
      out.push_back(key.triangle);
    }
    out.push_back(p.complete);
  }
  for (const auto& s : e.sites.sites)
    for (auto terms : s.pressure_offset_terms)
      for (double term : terms)
        number(term);
  for (const auto& q : d.load.source_quads) {
    number(q.minimum_edge_squared.lower);
    number(q.minimum_edge_squared.upper);
    number(q.minimum_signed_side.lower);
    number(q.minimum_signed_side.upper);
    out.push_back(q.checked_edges);
    out.push_back(q.checked_side_signs);
    out.push_back(q.valid);
  }
  out.push_back(e.complete);
  out.push_back(d.self.complete);
  out.push_back(d.load.complete);
  return out;
}
auto clock(double g) -> double {
  return g <= .25 ? 48 * g : g <= .5 ? 12 + 48 * (g - .25) : 24 + 24 * (g - .5);
}
auto local(std::size_t index, double g) -> double {
  return index == 0 ? 4 * g : index == 1 ? 4 * g - 1 : 2 * g - 1;
}
void denied(const Diagnostic& d) {
  check(!d.world_qualified && !d.material_qualified &&
            !d.source_surface_sweep_qualified && !d.route_qualified &&
            !d.actor_qualified && !d.seat_qualified && !d.save_qualified &&
            !d.first_flight_qualified && !d.free_foot_swing_qualified &&
            !d.dynamics_qualified && !d.strength_qualified &&
            !d.friction_qualified,
        "New self certificate grants no world, material, route, force or actor "
        "authority");
}
// Unchanged full body/COM/timing/rotated support and pressure kernels retain
// their independent phase, port-unload and checkpoint test suites. This test
// adds the exact capsule-complement expression and new decision-table oracle.
auto nominal_axis(const BoardingRouteFootPhaseRequest& r, std::size_t side,
                  double u) -> Wide {
  const long double t = u, S = t * t * t * (10 - 15 * t + 6 * t * t);
  auto sum = [](const BoardingRoutePhaseConstant& c) {
    long double n = 0;
    for (std::size_t i = 0; i < c.count; ++i)
      n += static_cast<long double>(c.terms[i]);
    return n;
  };
  Wide hip{}, sole{};
  const auto q =
      static_cast<long double>(r.root_yaw_half[0]) +
      (static_cast<long double>(r.root_yaw_half[1]) - r.root_yaw_half[0]) * S;
  const auto angle = 2 * std::atan(q), c = std::cos(angle),
             sn = std::sin(angle), sign = side == 0 ? -1.L : 1.L;
  for (std::size_t i = 0; i < 3; ++i) {
    const auto a = sum(r.root[0].coordinates[i]),
               b = sum(r.root[1].coordinates[i]);
    hip[i] = a + (b - a) * S;
    sole[i] = sum(r.feet[side].sole[0].coordinates[i]);
  }
  hip[0] += sign * static_cast<long double>(.14) * c;
  hip[2] -= sign * static_cast<long double>(.14) * sn;
  sole[1] += static_cast<long double>(.10);
  const auto d = add(sole, scale(hip, -1));
  const auto D = dot(d, d), rho = std::hypot(d[0], d[1]),
             L = static_cast<long double>(.47285),
             L2 = static_cast<long double>(.47478),
             alpha = (L * L - L2 * L2 + D) / (2 * D),
             gamma = std::sqrt((L * L - alpha * alpha * D) / D);
  const Wide N{-d[1] / rho, d[0] / rho, 0},
      cross{N[1] * d[2], -N[0] * d[2], N[0] * d[1] - N[1] * d[0]};
  return scale(add(scale(d, alpha), scale(cross, gamma)), 1 / L);
}
void cell_evidence(const Diagnostic& d, const Cell& c) {
  const auto& a = c.assessment;
  check(c.phase_index < 3 &&
            a.phase.first == local(c.phase_index, c.global_first) &&
            a.phase.last == local(c.phase_index, c.global_last),
        "Same immutable phase and local clock retained in new geometry packet");
  check(a.complete && a.phase.complete && a.self_complete &&
            a.nonnegative_reactions &&
            a.nominal_vertical_equilibrium_complete &&
            a.finite_pressure_complete,
        "Every accepted self02 cell still requires original "
        "graph/timing/pressure proofs");
  std::array<std::uint16_t, 7> counts{};
  std::array<std::uint16_t, 6> old_counts{};
  for (std::size_t pair = 0; pair < 105; ++pair) {
    const auto certificate = c.self02_certificates[pair];
    const auto kind = static_cast<std::size_t>(certificate);
    check(kind > 0 && kind < counts.size(),
          "All105 selected-policy decisions carry a genuine named certificate");
    if (kind < counts.size()) ++counts[kind];
    const auto old = a.pair_certificates[pair];
    if (certificate == Certificate::original_capsule_slab_complement)
      check(old == BoardingSourceEndpointSelfCertificate::none,
            "Complement-only acceptance never forges a passing original "
            "certificate");
    else
      check(kind == static_cast<std::size_t>(old),
            "Unchanged certificates retain exact original kind");
    if (old != BoardingSourceEndpointSelfCertificate::none)
      ++old_counts[static_cast<std::size_t>(old)];
  }
  check(counts == c.self02_certificate_counts &&
            old_counts == a.certificate_counts,
        "Seven-kind and original six-kind totals independently reconcile "
        "decisions");
  for (const auto& h : c.hip_complements) {
    if (!h.attempted) continue;
    check(h.side < 2 && h.region < 14 && h.pair < 105 &&
              h.arithmetic_supported && h.nominal_unit_identity &&
              h.upright_pelvis_identity && h.original_slab_identity &&
              h.slab_limit_metres == kBoardingSelfHipLengthMetres,
          "Attempted complement binds original region/axis/upright/slab "
          "compiled identities");
    if (h.side >= 2 || h.region >= 14 || h.pair >= 105) continue;
    const auto& r = d.initial->self.regions[h.region];
    check(r.junction == BoardingSelfJunction::hip &&
              r.limit_metres == kBoardingSelfHipLengthMetres,
          "No enlarged hip region or fake distal hemisphere enters proof");
    const auto& old = a.owners[h.region];
    check(old.arithmetic_supported && !old.certified &&
              old.certificate == BoardingSourceEndpointSelfCertificate::
                                     original_axis_box_support,
          "Original supported failed whole-pelvis owner remains visible and "
          "unchanged");
    for (double u : {a.phase.first, std::midpoint(a.phase.first, a.phase.last),
                     a.phase.last}) {
      const auto axis = nominal_axis(d.controls[c.phase_index], h.side, u);
      check(std::abs(dot(axis, axis) - 1) < 2e-12L,
            "Independent unfactored knee construction retains original nominal "
            "thigh length");
      for (std::size_t k = 0; k < 3; ++k)
        check(contains(h.axis[k], axis[k]),
              "Authentic axis bounds enclose independent original "
              "knee-minus-hip unit expression");
      const auto E =
          static_cast<long double>(kBoardingSelfHipLengthMetres) * axis[1] +
          static_cast<long double>(.105) * std::hypot(axis[0], axis[2]);
      if (h.extent_evaluated)
        check(contains(h.extent, E),
              "Retained complement encloses actual nominal geometric extent");
    }
    // The selected monotone expression reaches its upper bound with uY.upper
    // and maximal transverse component magnitudes. Independent LD arithmetic
    // corroborates retained expression bounds; it is never a new permission.
    const auto x = std::max(
                   std::abs(static_cast<long double>(h.axis[0].lower)),
                   std::abs(static_cast<long double>(h.axis[0].upper))),
               z = std::max(
                   std::abs(static_cast<long double>(h.axis[2].lower)),
                   std::abs(static_cast<long double>(h.axis[2].upper))),
               transverse = std::hypot(x, z),
               extent = static_cast<long double>(kBoardingSelfHipLengthMetres) *
                            h.axis[1].upper +
                        static_cast<long double>(.105) * transverse;
    if (h.extent_evaluated)
      check(contains(h.transverse, transverse) && contains(h.extent, extent) &&
                contains(h.limit, static_cast<long double>(-.12)) &&
                contains(h.strict_gap, static_cast<long double>(-.12) - extent),
            "Independent nominal axis complement expression and original "
            "pelvis bottom enclosed");
    if (h.certified)
      check(h.extent_evaluated && h.axis[1].upper < 0 &&
                h.extent.upper < h.limit.lower && h.strict_gap.lower > 0 &&
                c.self02_certificates[h.pair] ==
                    Certificate::original_capsule_slab_complement,
            "Accepted complement is strictly below original full pelvis and "
            "uses only new named policy");
  }
  const auto source =
      d.initial->self.endpoint.sites.source.selected_partitions();
  for (std::size_t side = 0; side < 2; ++side) {
    const auto& p = a.pressures[side];
    check(p.complete && p.arithmetic_supported && p.coplanar &&
              p.sole_disk_contained && p.source_disk_contained &&
              p.coverage_complete && p.scanned_partitions == 10 &&
              p.source_partition < source.size(),
          "New self proof leaves actual finite source and sole disks "
          "independently complete");
    if (p.source_partition < source.size())
      check(source[p.source_partition].plane_metres == d.controls[c.phase_index]
                                                           .feet[side]
                                                           .sole[0]
                                                           .coordinates[1]
                                                           .terms[0],
            "Actual selected pressure keeps original authentic source plane");
  }
  check(a.endpoint_zero_port_reaction ==
            (c.phase_index == 2 && a.phase.last == 1),
        "Only finish endpoint retains zero-port witness");
}
void accounting(const Diagnostic& d, const Limits& l = {}) {
  denied(d);
  check(
      d.self_policy_version == 2 && d.unload_version == 1 &&
          d.initial != nullptr,
      "New selected self policy retains one original checkpoint source chain");
  if (!d.initial) return;
  check(d.hip_complement_attempts <= l.hip_complement_attempts &&
            d.hip_complement_attempts <= 2 * d.work.phase.graphs &&
            d.work.ownership_attempts <= 14 * d.work.phase.graphs &&
            d.work.contact.self_pairs <= 105 * d.work.phase.graphs,
        "New proof attempts are bounded separately without extra pairs or "
        "owner regions");
  check(d.examined_nodes <= l.phase.nodes &&
            d.mandatory_splits <= d.examined_nodes &&
            d.work.phase.graphs <= d.examined_nodes - d.mandatory_splits &&
            d.output_capacity_bytes <= l.output_bytes &&
            d.maximum_depth <= l.phase.depth,
        "One shared global cover and actual owned output remain bounded");
  check(d.reporting_elapsed_seconds ==
            std::abs(clock(d.requested_last) - clock(d.requested_first)),
        "Selected new proof leaves piecewise physical clock unchanged");
  const auto& w = d.work;
  check(w.phase.graphs <= l.phase.graphs && w.phase.legs <= l.phase.legs &&
            w.phase.bodies <= l.phase.bodies &&
            w.phase.sectors <= l.phase.sectors &&
            w.phase.timing <= l.phase.timing &&
            w.contact.self_pairs <= l.pairs &&
            w.contact.proposed_axes <= l.axes &&
            w.contact.signed_trials <= l.signed_trials &&
            w.ownership_attempts <= l.owners &&
            w.contact.pressure_candidates <= l.candidates &&
            w.contact.disk_edges <= l.edges && d.cells.size() <= l.phase.leaves,
        "Original aggregate capacities stay charged across all three phases");
  check(w.contact.proposed_axes <= 14 * w.contact.self_pairs &&
            w.contact.signed_trials <= 2 * w.contact.proposed_axes &&
            w.contact.pressure_candidates <= 20 * w.phase.graphs &&
            w.contact.disk_edges <= 88 * w.phase.graphs,
        "Inherited finite separating and pressure work relationships remain "
        "bounded");
  if (d.initial->load.complete)
    check(d.owned_initial_quad_guards == 10 &&
              d.owned_initial_quad_edges == 40 &&
              d.owned_initial_convexity_signs == 80 &&
              d.owned_initial_triangle_windings == 20 &&
              d.owned_initial_diagonal_incidence_guards == 10,
          "Exactly one immutable initial chain earns original actual quad "
          "guards once");
  double next = std::min(d.requested_first, d.requested_last);
  for (const auto& c : d.cells) {
    check(c.global_first == next,
          "Accepted global cells remain a genuine contiguous prefix");
    next = c.global_last;
    cell_evidence(d, c);
  }
  if (d.complete)
    check(!d.first_refusal &&
              next == std::max(d.requested_first, d.requested_last) &&
              d.self_complete && d.support_complete &&
              d.finite_pressure_complete &&
              d.nominal_vertical_equilibrium_complete,
          "New complete result requires actual entire selected "
          "graph/self/pressure cover");
  else
    check(d.first_refusal.has_value(),
          "Refused selected policy retains honest prefix and attribution");
}
void observe(std::string_view label, const Diagnostic& d) {
  std::cout << std::setprecision(17) << label << " complete=" << d.complete
            << " cells=" << d.cells.size() << " nodes=" << d.examined_nodes
            << " graphs=" << d.work.phase.graphs
            << " hip_attempts=" << d.hip_complement_attempts
            << " joins=" << d.qualified_joins[0] << ',' << d.qualified_joins[1];
  if (d.first_refusal) {
    const auto& r = *d.first_refusal;
    std::cout << " refusal=" << static_cast<unsigned>(r.condition)
              << " self02=" << static_cast<unsigned>(r.self02_condition)
              << " predicate=" << static_cast<unsigned>(r.predicate_condition)
              << " global=" << r.first << ',' << r.last;
    if (r.phase_index) std::cout << " phase=" << *r.phase_index;
    if (r.pair) std::cout << " pair=" << *r.pair;
  }
  if (!d.cells.empty())
    std::cout << " prefix=" << d.cells.front().global_first << ','
              << d.cells.back().global_last;
  std::cout << '\n';
  std::cout.flush();
}
auto decision_snapshot(const Diagnostic& d) -> std::vector<std::uint64_t> {
  std::vector<std::uint64_t> out;
  auto number = [&](double x) {
    out.push_back(std::bit_cast<std::uint64_t>(x));
  };
  auto bound = [&](Bounds b) {
    number(b.lower);
    number(b.upper);
  };
  out.push_back(d.complete);
  out.push_back(d.hip_complement_attempts);
  for (const auto& c : d.cells) {
    number(c.global_first);
    number(c.global_last);
    out.push_back(c.phase_index);
    for (auto certificate : c.self02_certificates)
      out.push_back(static_cast<std::uint64_t>(certificate));
    for (auto n : c.self02_certificate_counts)
      out.push_back(n);
    for (const auto& h : c.hip_complements) {
      out.push_back(h.attempted);
      out.push_back(h.certified);
      out.push_back(h.extent_evaluated);
      out.push_back(h.side);
      out.push_back(h.region);
      out.push_back(h.pair);
      for (auto b : h.axis)
        bound(b);
      for (auto b : std::array{h.transverse, h.extent, h.limit, h.strict_gap})
        bound(b);
    }
    for (const auto& o : c.assessment.owners) {
      bound(o.extent);
      bound(o.limit);
      bound(o.secondary_extent);
      out.push_back(o.certified);
      out.push_back(static_cast<std::uint64_t>(o.certificate));
    }
    for (const auto& p : c.assessment.pressures) {
      for (auto b : p.pressure_xz)
        bound(b);
      out.push_back(p.source_partition);
      out.push_back(p.complete);
    }
  }
  if (d.first_refusal) {
    out.push_back(static_cast<std::uint64_t>(d.first_refusal->condition));
    out.push_back(
        static_cast<std::uint64_t>(d.first_refusal->self02_condition));
    number(d.first_refusal->first);
    number(d.first_refusal->last);
  }
  return out;
}
void stale(Cell& c) {
  c.assessment.complete = c.assessment.self_complete =
      c.assessment.finite_pressure_complete =
          c.assessment.arithmetic_supported = true;
  c.assessment.phase.complete = true;
  for (auto& h : c.hip_complements)
    h.attempted = h.certified = h.nominal_unit_identity = true;
  c.self02_certificates.fill(Certificate::original_capsule_slab_complement);
  c.self02_certificate_counts.fill(105);
}
void reset(const Cell& c) {
  check(!c.assessment.complete && !c.assessment.self_complete &&
            !c.assessment.finite_pressure_complete &&
            !c.assessment.phase.complete,
        "Early refusal clears stale caller acceptance without admitting caller "
        "graph");
  for (const auto& h : c.hip_complements)
    check(!h.attempted && !h.certified && !h.nominal_unit_identity,
          "Early refusal clears stale complement identity/certificate");
  check(std::all_of(c.self02_certificates.begin(), c.self02_certificates.end(),
                    [](auto v) { return v == Certificate::none; }) &&
            std::all_of(c.self02_certificate_counts.begin(),
                        c.self02_certificate_counts.end(),
                        [](auto v) { return v == 0; }),
        "Early refusal clears selected decision table and counts");
}
void invalid_controls(const OriginBoardingBootSupport& source) {
  for (double v :
       {-1., std::nextafter(1., 2.), std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::quiet_NaN()})
    check(
        !assess_origin_boarding_route_checkpoint_unload_self02(source, v, 1) &&
            !assess_origin_boarding_route_checkpoint_unload_self02(source, 0,
                                                                   v),
        "Invalid/nonfinite global clock refuses before source or graph "
        "admission");
  auto copy = source;
  const auto retained = std::move(copy);
  // NOLINTBEGIN(bugprone-use-after-move) -- Documented empty provider must
  // refuse selected-policy admission.
  check(!assess_origin_boarding_route_checkpoint_unload_self02(copy),
        "Moved-from provider has no source capability");
  // NOLINTEND(bugprone-use-after-move) -- End empty-provider contract check.
  (void)retained;
  Limits raised;
  ++raised.hip_complement_attempts;
  check(!detail::boarding_route_checkpoint_unload_self02_bounded(source, 0, 1,
                                                                 raised),
        "New hip capacity cannot exceed registered4094 ceiling");
  raised = {};
  ++raised.phase.nodes;
  check(!detail::boarding_route_checkpoint_unload_self02_bounded(source, 0, 1,
                                                                 raised),
        "New method cannot raise original common cover cap");
  for (std::size_t cap = 0; cap < 3; ++cap) {
    Limits l;
    l.phase.nodes = cap;
    const auto d =
        require(detail::boarding_route_checkpoint_unload_self02_bounded(
            source, 0, 1, l));
    check(!d.complete && d.first_refusal &&
              d.first_refusal->condition == Condition::node_capacity &&
              d.examined_nodes == cap && d.mandatory_splits == cap &&
              d.work.phase.graphs == 0 && d.hip_complement_attempts == 0,
          "Original .5-then-.25 mandatory nodes consume budget before "
          "numerical graph");
    denied(d);
  }
  BoardingRouteCheckpointUnloadDiagnostic owner;
  auto context = require(detail::prepare_boarding_route_checkpoint_unload(
      source, Limits{}, owner));
  BoardingRoutePortUnloadCounters work;
  Cell out;
  stale(out);
  detail::BoardingRouteCheckpointUnloadSelf02CellRefusal why;
  std::uint64_t attempts = 1;
  Limits zero;
  zero.hip_complement_attempts = 0;
  auto state = detail::boarding_route_checkpoint_unload_self02_cell(
      context, 2, 0, 0, false, zero, work, attempts, out, why);
  check(state == State::capacity && why.hip_complement_capacity &&
            attempts == 1 && work.phase.graphs == 0,
        "Incoming over-cap new counter refuses before graph or increment");
  reset(out);
  work = {};
  attempts = kBoardingRouteCheckpointUnloadSelf02MaximumHipAttempts;
  stale(out);
  why = {};
  state = detail::boarding_route_checkpoint_unload_self02_cell(
      context, 2, 0, 0, false, Limits{}, work, attempts, out, why);
  check(state == State::capacity && why.hip_complement_capacity &&
            attempts ==
                kBoardingRouteCheckpointUnloadSelf02MaximumHipAttempts &&
            work.phase.graphs == 1 && work.contact.self_pairs > 0,
        "Incoming counter exactly at4094 ceiling reaches original graph/owner "
        "before actual next attempt refuses");
  work = {};
  attempts = 0;
  stale(out);
  why = {};
  state = detail::boarding_route_checkpoint_unload_self02_cell(
      context, 3, 0, 0, false, Limits{}, work, attempts, out, why);
  check(state == State::unsupported &&
            why.condition == Condition::invalid_binding &&
            work.phase.graphs == 0 && attempts == 0,
        "Unknown phase cannot enroll caller policy or stale acceptance");
  reset(out);
  const auto kept = std::move(context);
  (void)kept;
  stale(out);
  why = {};
  // NOLINTBEGIN(bugprone-use-after-move) -- Documented empty source context
  // must refuse and clear stale output.
  state = detail::boarding_route_checkpoint_unload_self02_cell(
      context, 2, 0, 0, false, Limits{}, work, attempts, out, why);
  // NOLINTEND(bugprone-use-after-move) -- End empty-context contract check.
  check(state == State::unsupported &&
            why.condition == Condition::invalid_binding &&
            work.phase.graphs == 0,
        "Moved context cannot lend caller-cell source authority");
  reset(out);
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
    const auto m = detail::boarding_route_checkpoint_unload_self02_hip_math(
        {Bounds{0, 0}, Bounds{-1, -1}, Bounds{0, 0}}, .105,
        kBoardingSelfHipLengthMetres, -.12);
    check(
        !m || (!m->arithmetic_supported && !m->strict_extent_below_bottom &&
               !m->unit_qualified && !m->self_qualified),
        "Unsafe environment refuses raw clipping arithmetic without authority");
    const auto d =
        assess_origin_boarding_route_checkpoint_unload_self02(source);
    check(!d ||
              (!d->complete && !d->arithmetic_supported && !d->self_complete &&
               !d->support_complete && d->hip_complement_attempts == 0),
          "Unsafe environment cannot retain selected-policy graph or self "
          "qualification");
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
void original_refusals(const OriginBoardingBootSupport& source) {
  const auto old =
      require(assess_origin_boarding_route_checkpoint_unload(source));
  check(!old.complete && old.first_refusal &&
            old.first_refusal->condition == Condition::depth_capacity &&
            old.first_refusal->predicate_condition ==
                Condition::unresolved_self_pair &&
            old.first_refusal->phase_index == 1 &&
            old.first_refusal->first == .4658203125 &&
            old.first_refusal->last == .466796875 && !old.cells.empty() &&
            old.cells.back().global_last == .4658203125 &&
            old.qualified_joins == std::array<bool, 2>{true, false},
        "Version1 retains frozen original whole unresolved-owner refusal and "
        "valid prefix");
  const auto point =
      require(assess_origin_boarding_route_checkpoint_unload(source, .5, .5));
  check(!point.complete && point.first_refusal &&
            point.first_refusal->condition ==
                Condition::unsplittable_interval &&
            point.first_refusal->predicate_condition ==
                Condition::unresolved_self_pair &&
            point.cells.empty(),
        "Version1 exact second join remains unresolved rather than "
        "reclassified as collision");
  BoardingRouteCheckpointUnloadDiagnostic owner;
  const auto context = require(detail::prepare_boarding_route_checkpoint_unload(
      source, Limits{}, owner));
  BoardingRoutePortUnloadCounters work;
  BoardingRoutePortUnloadCell out;
  BoardingRoutePortUnloadRefusal why;
  const auto state = detail::boarding_route_checkpoint_unload_cell(
      context, 2, 0, 0, false, Limits{}, work, out, why);
  check(state == State::unresolved &&
            why.condition == Condition::unresolved_self_pair && why.pair == 2 &&
            out.owners[1].arithmetic_supported && !out.owners[1].certified &&
            out.owners[1].extent.upper > out.owners[1].limit.lower,
        "Authentic original failed hip margin remains visible without clipping "
        "relabel");
}
void observed_budgets(const OriginBoardingBootSupport& source) {
  const auto base = require(
      assess_origin_boarding_route_checkpoint_unload_self02(source, .5, .5));
  observe("HIP_CAP_BASE_POINT", base);
  accounting(base);
  check(base.hip_complement_attempts > 0,
        "Authentic formerly unresolved join reaches new complement method");
  Limits zero;
  zero.hip_complement_attempts = 0;
  const auto z =
      require(detail::boarding_route_checkpoint_unload_self02_bounded(
          source, .5, .5, zero));
  check(!z.complete && z.first_refusal &&
            z.first_refusal->self02_condition ==
                BoardingRouteCheckpointUnloadSelf02Condition::
                    hip_complement_capacity &&
            z.hip_complement_attempts == 0 && z.work.phase.graphs > 0 &&
            z.work.contact.self_pairs > 0,
        "Zero hip cap reaches genuine original failed owner after graph/pair "
        "work");
  accounting(z, zero);
  if (base.hip_complement_attempts > 0) {
    Limits exact;
    exact.hip_complement_attempts = base.hip_complement_attempts;
    const auto replay =
        require(detail::boarding_route_checkpoint_unload_self02_bounded(
            source, .5, .5, exact));
    accounting(replay, exact);
    check(decision_snapshot(replay) == decision_snapshot(base) &&
              old_child(*replay.initial) == old_child(*base.initial),
          "Exact genuinely used hip budget preserves full decision and "
          "unchanged source evidence");
    --exact.hip_complement_attempts;
    const auto less =
        require(detail::boarding_route_checkpoint_unload_self02_bounded(
            source, .5, .5, exact));
    check(!less.complete && less.first_refusal &&
              less.first_refusal->self02_condition ==
                  BoardingRouteCheckpointUnloadSelf02Condition::
                      hip_complement_capacity &&
              less.hip_complement_attempts == exact.hip_complement_attempts,
          "One less actual complement operation refuses at real attempt "
          "boundary");
    accounting(less, exact);
  }
  const auto point = require(
      assess_origin_boarding_route_checkpoint_unload_self02(source, 0, 0));
  accounting(point);
  if (!point.complete) return;
  Limits exact;
  exact.phase.nodes = point.examined_nodes;
  exact.phase.leaves = point.cells.size();
  exact.output_bytes = point.output_capacity_bytes;
  exact.phase.graphs = point.work.phase.graphs;
  exact.phase.legs = point.work.phase.legs;
  exact.phase.bodies = point.work.phase.bodies;
  exact.phase.sectors = point.work.phase.sectors;
  exact.phase.timing = point.work.phase.timing;
  exact.pairs = point.work.contact.self_pairs;
  exact.axes = point.work.contact.proposed_axes;
  exact.signed_trials = point.work.contact.signed_trials;
  exact.owners = point.work.ownership_attempts;
  exact.candidates = point.work.contact.pressure_candidates;
  exact.edges = point.work.contact.disk_edges;
  exact.hip_complement_attempts = point.hip_complement_attempts;
  const auto replay =
      require(detail::boarding_route_checkpoint_unload_self02_bounded(
          source, 0, 0, exact));
  check(replay.complete &&
            decision_snapshot(replay) == decision_snapshot(point),
        "Exact actually reached original graph/self/pressure/storage caps "
        "preserve original point evidence");
  accounting(replay, exact);
  for (int stage = 0; stage < 6; ++stage) {
    auto l = exact;
    std::uint64_t* cap{};
    Condition expected{};
    switch (stage) {
      case 0:
        cap = &l.pairs;
        expected = Condition::pair_capacity;
        break;
      case 1:
        cap = &l.axes;
        expected = Condition::axis_capacity;
        break;
      case 2:
        cap = &l.signed_trials;
        expected = Condition::signed_trial_capacity;
        break;
      case 3:
        cap = &l.owners;
        expected = Condition::ownership_capacity;
        break;
      case 4:
        cap = &l.candidates;
        expected = Condition::pressure_capacity;
        break;
      default:
        cap = &l.edges;
        expected = Condition::edge_capacity;
        break;
    }
    if (*cap == 0) continue;
    --*cap;
    const auto d =
        require(detail::boarding_route_checkpoint_unload_self02_bounded(
            source, 0, 0, l));
    check(
        !d.complete && d.first_refusal &&
            d.first_refusal->condition == expected,
        "One less reached original work operation retains exact stage refusal");
    accounting(d, l);
  }
}
void cover_and_reverse(const OriginBoardingBootSupport& source) {
  for (std::size_t phase = 0; phase < 3; ++phase) {
    const std::array<double, 4> boundaries{0, .25, .5, 1};
    const auto a = boundaries[phase], b = boundaries[phase + 1],
               x = a + (b - a) * .25, y = a + (b - a) * .75;
    const auto f = require(
                   assess_origin_boarding_route_checkpoint_unload_self02(source,
                                                                         x, y)),
               r = require(
                   assess_origin_boarding_route_checkpoint_unload_self02(source,
                                                                         y, x));
    accounting(f);
    accounting(r);
    if (f.complete && r.complete) {
      check(f.cells.size() == r.cells.size(),
            "Reverse retains same canonical accepted cover");
      for (std::size_t i = 0; i < std::min(f.cells.size(), r.cells.size());
           ++i) {
        const auto& fc = f.cells[i];
        const auto& rc = r.cells[i];
        check(fc.global_first == rc.global_first &&
                  fc.global_last == rc.global_last &&
                  fc.phase_index == rc.phase_index,
              "Reverse canonical cells and selected phase unchanged");
        for (std::size_t p = 0; p < 18; ++p) {
          const auto& fd = fc.assessment.phase.points[p].derivatives;
          const auto& rd = rc.assessment.phase.points[p].derivatives;
          check(fd.acceleration.lower == rd.acceleration.lower &&
                    fd.acceleration.upper == rd.acceleration.upper,
                "Reverse preserves physical second derivatives");
          for (std::size_t axis = 0; axis < 3; ++axis)
            check(component(fd.velocity.lower, axis) ==
                          -component(rd.velocity.upper, axis) &&
                      component(fd.velocity.upper, axis) ==
                          -component(rd.velocity.lower, axis),
                  "Reverse negates first derivatives without global-clock "
                  "rescaling");
        }
      }
    }
  }
  for (std::size_t join = 0; join < 2; ++join) {
    const double g = join == 0 ? .25 : .5;
    const auto p = require(
        assess_origin_boarding_route_checkpoint_unload_self02(source, g, g));
    accounting(p);
    check(p.qualified_joins == std::array<bool, 2>{false, false},
          "Exact join point cannot invent two-sided physical qualification");
    if (p.complete)
      check(p.cells.size() == 1 && p.cells.front().phase_index == join + 1 &&
                p.cells.front().assessment.phase.first == 0,
            "Exact internal tie chooses right phase with local zero");
    const auto cross =
        require(assess_origin_boarding_route_checkpoint_unload_self02(
            source, g - 1. / 1024, g + 1. / 1024));
    accounting(cross);
    if (cross.complete)
      check(cross.qualified_joins[join],
            "Actual accepted neighboring nonpoint cells earn physical join");
  }
}
void public_cases(const OriginBoardingBootSupport& source) {
  const auto original =
      old_child(require(assess_origin_boarding_source_endpoint_load(source)));
  auto outcome = assess_origin_boarding_route_checkpoint_unload_self02(source);
  if (!outcome) {
    std::cout << "FIRST_PUBLIC_CHECKPOINT_UNLOAD_SELF02 API_ERROR="
              << outcome.error() << '\n';
    std::cout.flush();
  }
  const auto first = require(std::move(outcome));
  observe("FIRST_PUBLIC_CHECKPOINT_UNLOAD_SELF02", first);
  accounting(first);
  check(old_child(*first.initial) == original,
        "Selected policy retains one unchanged genuine initial child");
  // FIRST precedes any outcome-dependent checks; selected full-path outcome
  // remains unknown until Root freezes and records both compiler observations.
  observed_budgets(source);
  for (auto range : std::array<std::array<double, 2>, 9>{{{1, 0},
                                                          {0, .25},
                                                          {.25, .5},
                                                          {.5, 1},
                                                          {.125, .75},
                                                          {.75, .125},
                                                          {0, 0},
                                                          {1, 1},
                                                          {.5, .5}}}) {
    const auto d =
        require(assess_origin_boarding_route_checkpoint_unload_self02(
            source, range[0], range[1]));
    observe("REQUEST", d);
    accounting(d);
    check(old_child(*d.initial) == original,
          "Every fresh full/reverse/sub/point retains original initial source "
          "contracts");
    if (d.complete && range[0] == 1 && range[1] == 1) {
      const auto& c = d.cells.front().assessment;
      check(d.endpoint_zero_port_reaction &&
                c.port_reaction_fraction.lower == 0 &&
                c.port_reaction_fraction.upper == 0,
            "Exact global1 retains actual zero-port single-foot endpoint");
      for (std::size_t axis = 0; axis < 2; ++axis) {
        const auto k = axis == 0 ? 0U : 2U;
        check(c.pressures[1].pressure_xz[axis].lower ==
                      component(c.phase.center_of_mass.value.lower, k) &&
                  c.pressures[1].pressure_xz[axis].upper ==
                      component(c.phase.center_of_mass.value.upper, k),
              "Actual finish pressure equals same full moving COM bounds");
      }
    }
  }
  cover_and_reverse(source);
  check(old_child(require(
            assess_origin_boarding_source_endpoint_load(source))) == original,
        "New self method never mutates source child or original APIs");
}
} // namespace
int main() {
  try {
    static_assert(sizeof(Cell) <=
                  kBoardingRouteCheckpointUnloadMaximumCellBytes);
    static_assert(
        !std::is_constructible_v<detail::BoardingRouteCheckpointUnloadCellToken,
                                 const detail::BoardingRoutePortUnloadContext&,
                                 const BoardingRoutePortUnloadCell&,
                                 std::size_t>);
    const auto binding = require(make_native_starting_assembly_binding(
        NativeStartingAssemblySelection{}));
    const auto source = require(make_origin_boarding_boot_support(binding));
    invalid_controls(source);
    unsafe_environment(source);
    theorem_geometry();
    expression_controls();
    original_refusals(source);
    public_cases(source);
  } catch (const std::exception& e) {
    ++failures;
    std::cerr << "EXCEPTION: " << e.what() << '\n';
  }
  std::cout << checks << " checks, " << failures << " failures\n";
  return failures == 0 ? 0 : 1;
}
