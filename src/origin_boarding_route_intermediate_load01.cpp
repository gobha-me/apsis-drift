#include "origin_boarding_route_intermediate_load01_internal.hpp"
#include "origin_boarding_route_intermediate_step02_internal.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <new>
namespace apsis_drift {
namespace {
using D = BoardingRouteIntermediateLoad01Diagnostic;

using C = BoardingRouteIntermediateLoad01Cell;

using R = BoardingRouteIntermediateLoad01Refusal;

using W = BoardingRouteIntermediateLoad01Condition;

using S = BoardingRouteIntermediateLoad01State;

using L = detail::BoardingRouteIntermediateLoad01Limits;

using B = BoardingFootSiteScalarBounds;

using E = std::expected<D, std::string>;

using A = detail::BoardingIntermediatePauseSupportAccess;

using Request = BoardingRouteFootPhaseRequest;

using P = BoardingPlantedBodyPointId;

using Force = BoardingRouteIntermediateLoad01PortForceState;

struct Pending {
  double first{}, last{};
  std::size_t depth{};
};

constexpr std::size_t fixed_output = 2 * sizeof(E);

constexpr std::size_t source_live =
    32768 + 2 * sizeof(E) + 4096 + 2048 + 4 * sizeof(L) + 2048 +
    11 * sizeof(Pending) + sizeof(detail::BoardingRouteFootPhaseLimits) +
    sizeof(BoardingRouteFootPhaseRefusal) + sizeof(std::optional<Request>);

static_assert(sizeof(E) <= 3072);

static_assert(sizeof(C) <= kBoardingRouteIntermediateLoad01MaximumCellBytes);

static_assert(sizeof(R) <= 248);

static_assert(sizeof(L) == 176);
static_assert(
    sizeof(C) - sizeof(BoardingRouteFootPhaseCell) + sizeof(R) +
        sizeof(detail::BoardingRouteIntermediateLoad01AdmissionContext) +
        2 * sizeof(Pending) + 64 <=
    2048);

static_assert(source_live <=
              kBoardingRouteIntermediateLoad01MaximumScratchBytes);

static_assert(fixed_output + 1024 * sizeof(C) <=
              kBoardingRouteIntermediateLoad01MaximumOutputBytes);

auto valid(B x, double domain) -> bool {
  return x.supported && std::isfinite(x.lower) && std::isfinite(x.upper) &&
         x.lower <= x.upper && std::abs(x.lower) <= domain &&
         std::abs(x.upper) <= domain;
}
auto point(double x) -> B {
  return {x, x, true};
}
auto down(double x) -> double {
  return std::nextafter(x, -std::numeric_limits<double>::infinity());
}
auto up(double x) -> double {
  return std::nextafter(x, std::numeric_limits<double>::infinity());
}
auto add(B a, B b) -> B {
  return {down(a.lower + b.lower), up(a.upper + b.upper),
          a.supported && b.supported};
}
auto sub(B a, B b) -> B {
  return add(a, {-b.upper, -b.lower, b.supported});
}
auto mul(B a, B b) -> B {
  const std::array p{a.lower * b.lower, a.lower * b.upper, a.upper * b.lower,
                     a.upper * b.upper};
  return {down(*std::min_element(p.begin(), p.end())),
          up(*std::max_element(p.begin(), p.end())),
          a.supported && b.supported};
}
auto minimum(B a, B b) -> B {
  return {std::min(a.lower, b.lower), std::min(a.upper, b.upper),
          a.supported && b.supported};
}
auto maximum(B a, B b) -> B {
  return {std::max(a.lower, b.lower), std::max(a.upper, b.upper),
          a.supported && b.supported};
}
auto intersect(B a, B b) -> B {
  return {std::max(a.lower, b.lower), std::min(a.upper, b.upper),
          a.supported && b.supported};
}
auto coord(RigidVector3 p, std::size_t a) -> double {
  return a == 0 ? p.x : a == 1 ? p.y : p.z;
}
auto same(const BoardingRoutePhaseConstant& a,
          const BoardingRoutePhaseConstant& b) -> bool {
  return a.count == b.count && a.terms == b.terms;
}
auto capacity(W w) -> bool {
  return w == W::source_guard_capacity || w == W::projection_capacity ||
         w == W::definition_capacity || w == W::reaction_capacity ||
         w == W::sole_extrema_capacity || w == W::source_coordinate_capacity ||
         w == W::intersection_capacity || w == W::midpoint_capacity ||
         w == W::allocation_capacity || w == W::division_quotient_capacity ||
         w == W::division_fold_capacity || w == W::pressure_capacity ||
         w == W::edge_capacity || w == W::output_capacity ||
         w == W::node_capacity || w == W::leaf_capacity;
}
auto fail(D& d, R& r, W w, std::optional<std::size_t> axis = {},
          std::optional<std::size_t> op = {}) -> bool {
  if (r.condition == W::none) {
    r.condition = r.predicate_condition = w;
    r.axis = axis;
    r.operation = op;
  }
  d.stop_condition = w;
  d.state = capacity(w)                      ? S::capacity
            : w == W::unsupported_arithmetic ? S::unsupported
                                             : S::unresolved;

  if (w == W::unsupported_arithmetic) d.arithmetic_supported = false;

  return false;
}
auto charge(D& d, R& r, std::size_t& used, std::size_t cap, W w,
            std::optional<std::size_t> axis = {},
            std::optional<std::size_t> op = {}) -> bool {
  if (used >= cap) return fail(d, r, w, axis, op);
  ++used;
  return true;
}
template <class F> auto guard(D& d, C& c, const L& l, R& r, F f, W w) -> bool {
  if (!detail::boarding_route_intermediate_load01_definition_charge(d, c, l, r))
    return false;
  if (!f()) return fail(d, r, w);
  return true;
}
auto projection_charge(D& d, const L& l, R& r) -> bool {
  return charge(d, r, d.work.projection_guards, l.projection_guards,
                W::projection_capacity);
}
auto carrier(const BoardingPlantedBodyPointEvidence& p, std::size_t index, D& d,
             C& c, const L& l, R& r) -> bool {
  for (std::size_t kind = 0; kind < 3; ++kind) {
    const auto& b = kind == 0   ? p.value
                    : kind == 1 ? p.derivatives.velocity
                                : p.derivatives.acceleration;

    for (std::size_t a = 0; a < 3; ++a) {
      if (!projection_charge(d, l, r)) return false;
      const auto lo = coord(b.lower, a);

      if (!std::isfinite(lo) || (kind == 0 && std::abs(lo) > 8) ||
          (index != 0 && kind != 0 && lo > 0))
        return fail(d, r, W::projection_identity);

      if (!projection_charge(d, l, r)) return false;
      const auto hi = coord(b.upper, a);

      if (!std::isfinite(hi) || lo > hi || (kind == 0 && std::abs(hi) > 8) ||
          (index != 0 && kind != 0 && hi < 0))
        return fail(d, r, W::projection_identity);
    }
  }
  c.projected_carrier_complete[index] = true;
  return true;
}
[[gnu::noinline]] auto projection(D& d, C& c, const Request& r, const L& l,
                                  R& reason) -> bool {
  const auto& p = c.phase;

  if (!projection_charge(d, l, reason)) return false;

  if (!A::valid(d.source) || !A::data(d.source))
    return fail(d, reason, W::invalid_binding);

  for (const auto* flag :
       std::array{&p.complete, &p.arithmetic_supported, &p.nominal_links,
                  &p.target_sole_identities, &p.joint_sectors,
                  &p.derivative_domains, &p.timing_complete}) {
    if (!projection_charge(d, l, reason)) return false;
    if (!*flag) return fail(d, reason, W::phase_prerequisite);
  }
  if (!projection_charge(d, l, reason)) return false;
  if (p.first != detail::boarding_route_intermediate_load01_local(
                     c.phase_index, c.global_first))
    return fail(d, reason, W::projection_identity);

  if (!projection_charge(d, l, reason)) return false;
  if (p.last != detail::boarding_route_intermediate_load01_local(c.phase_index,
                                                                 c.global_last))
    return fail(d, reason, W::projection_identity);

  for (std::size_t a = 0; a < 3; ++a) {
    if (!projection_charge(d, l, reason)) return false;
    if (!same(r.root[0].coordinates[a], r.root[1].coordinates[a]))
      return fail(d, reason, W::projection_identity);
  }
  for (std::size_t side = 0; side < 2; ++side) {
    for (std::size_t a = 0; a < 3; ++a) {
      if (!projection_charge(d, l, reason)) return false;
      if (!same(r.feet[side].sole[0].coordinates[a],
                r.feet[side].sole[1].coordinates[a]))
        return fail(d, reason, W::projection_identity);
    }
    if (!projection_charge(d, l, reason)) return false;
    if (r.feet[side].yaw_half != std::array<double, 2>{0, 0})
      return fail(d, reason, W::projection_identity);

    if (!projection_charge(d, l, reason)) return false;
    if (r.feet[side].swing_height_metres != 0)
      return fail(d, reason, W::projection_identity);
  }
  for (std::size_t check = 0; check < 4; ++check) {
    if (!projection_charge(d, l, reason)) return false;

    const bool ok =
        check == 0 ? r.root_yaw_half == std::array<double, 2>{.125, .125}
        : check == 1
            ? r.torso_lean_half == (c.phase_index == 3
                                        ? std::array<double, 2>{-.25, -.30}
                                        : std::array<double, 2>{-.30, -.30})
        : check == 2
            ? r.port_reaction_fraction ==
                  (c.phase_index == 3 ? std::array<double, 2>{0, .0625}
                                      : std::array<double, 2>{.0625, .0625})
            : r.seconds_per_parameter == (c.phase_index == 3 ? 12 : 2);

    if (!ok) return fail(d, reason, W::projection_identity);
  }
  for (std::size_t i = 0; i < d.parts.size(); ++i) {
    if (!projection_charge(d, l, reason)) return false;
    if (static_cast<std::size_t>(d.parts[i].id) != i)
      return fail(d, reason, W::projection_identity);
  }
  for (std::size_t side = 0; side < 2; ++side) {
    const auto index = static_cast<std::size_t>(
        side == 0 ? BoardingBodyPartId::port_boot
                  : BoardingBodyPartId::starboard_boot);

    if (!projection_charge(d, l, reason)) return false;
    const auto* b =
        std::get_if<BoardingRoutePhaseBoxBinding>(&d.parts[index].reservation);
    if (!b) return fail(d, reason, W::projection_identity);

    if (!projection_charge(d, l, reason)) return false;
    if (b->center !=
        (side == 0 ? P::port_boot_center : P::starboard_boot_center))
      return fail(d, reason, W::projection_identity);

    if (!projection_charge(d, l, reason)) return false;
    if (b->half_size_metres != RigidVector3{.06, .05, .14})
      return fail(d, reason, W::projection_identity);

    if (!projection_charge(d, l, reason)) return false;
    if (b->frame != (side == 0 ? BoardingRoutePhaseFrame::port_sole
                               : BoardingRoutePhaseFrame::starboard_sole))
      return fail(d, reason, W::projection_identity);

    if (!projection_charge(d, l, reason)) return false;
    const auto* src = A::partition(d.source, side);
    const auto& y = r.feet[side].sole[0].coordinates[1];

    if (!src || y.count != 1 ||
        y.terms != std::array<double, 3>{src->plane_metres, 0, 0})
      return fail(d, reason, W::sole_plane);
    c.sites[side].plane_identity = true;
  }
  if (!projection_charge(d, l, reason)) return false;
  if (!std::isfinite(p.port_reaction_fraction.lower) ||
      !std::isfinite(p.port_reaction_fraction.upper) ||
      p.port_reaction_fraction.lower > p.port_reaction_fraction.upper ||
      std::abs(p.port_reaction_fraction.lower) > 8 ||
      std::abs(p.port_reaction_fraction.upper) > 8)
    return fail(d, reason, W::projection_identity);

  std::size_t i{};
  for (const auto* x :
       std::array{&p.center_of_mass,
                  &p.points[static_cast<std::size_t>(P::port_boot_center)],
                  &p.points[static_cast<std::size_t>(P::starboard_boot_center)],
                  &p.points[static_cast<std::size_t>(P::port_ankle)],
                  &p.points[static_cast<std::size_t>(P::starboard_ankle)]})
    if (!carrier(*x, i++, d, c, l, reason)) return false;

  for (std::size_t side = 0; side < 2; ++side)
    for (const auto& x : p.frames[side + 2].columns)
      if (!carrier(x, i++, d, c, l, reason)) return false;

  c.projection_complete = true;
  return true;
}
// Each endpoint division and each min/max fold is an independently charged
// primitive; partial masks never publish the returned scalar.
template <class Q, class F>
[[gnu::noinline]] auto divide(B n, B den, B& out, std::array<B, 4>* saved,
                              std::array<bool, 4>& qm,
                              std::array<std::array<bool, 2>, 3>& fm, Q qcharge,
                              F fcharge) -> W {
  B run;

  for (std::size_t i = 0; i < 4; ++i) {
    if (!qcharge(i)) return W::division_quotient_capacity;
    qm[i] = true;

    const auto q =
        (i < 2 ? n.lower : n.upper) / (i % 2 == 0 ? den.lower : den.upper);
    const B b{down(q), up(q), true};

    if (saved) (*saved)[i] = b;
    if (!valid(b, 32)) return W::unsupported_arithmetic;

    if (i == 0) {
      run = b;
      continue;
    }
    if (!fcharge(i - 1, 0)) return W::division_fold_capacity;
    fm[i - 1][0] = true;
    run.lower = std::min(run.lower, b.lower);

    if (!fcharge(i - 1, 1)) return W::division_fold_capacity;
    fm[i - 1][1] = true;
    run.upper = std::max(run.upper, b.upper);
  }
  out = run;
  return W::none;
}
auto valid_limits(const L& l) -> bool {
  const L m;
  const auto& a = l.phase;
  const auto& b = m.phase;

  return a.depth <= b.depth && a.nodes <= b.nodes && a.leaves <= b.leaves &&
         a.output_bytes <= b.output_bytes && a.graphs <= b.graphs &&
         a.legs <= b.legs && a.bodies <= b.bodies && a.sectors <= b.sectors &&
         a.timing <= b.timing && l.source_guards <= m.source_guards &&
         l.projection_guards <= m.projection_guards &&
         l.definition_guards <= m.definition_guards &&
         l.pressure_candidates <= m.pressure_candidates &&
         l.disk_edges <= m.disk_edges && l.sole_extrema <= m.sole_extrema &&
         l.source_coordinates <= m.source_coordinates &&
         l.intersection_operations <= m.intersection_operations &&
         l.midpoint_operations <= m.midpoint_operations &&
         l.allocation_operations <= m.allocation_operations &&
         l.reaction_operations <= m.reaction_operations &&
         l.division_quotients <= m.division_quotients &&
         l.division_folds <= m.division_folds;
}
[[gnu::noinline]] auto enroll(D& d, const L& l, R& r) -> bool {
  const BoardingIntermediatePauseConstructorEvidence* summary{};

  const BoardingBootSourcePartition* port{};
  const BoardingBootSourcePartition* star{};

  for (std::size_t i = 0; i < 18; ++i) {
    if (!charge(d, r, d.work.source_guards, l.source_guards,
                W::source_guard_capacity))
      return false;
    d.source_evaluated[i] = true;
    bool ok = false;

    switch (i) {
      case 0: ok = A::valid(d.source); break;

      case 1:
        ok = detail::boarding_route_foot_phase_environment();
        if (!ok) return fail(d, r, W::unsupported_arithmetic);
        d.arithmetic_supported = true;
        break;

      case 2:
        summary = d.source.summary();
        ok = summary && summary->complete && summary->version == 1 &&
             summary->actual_source_bytes == 4096;
        break;

      case 3:
        port = A::partition(d.source, 0);
        ok = port != nullptr;
        break;

      case 4:
        star = A::partition(d.source, 1);
        ok = star != nullptr;
        break;

      case 5:
        ok = summary->sources[0].keys ==
                 std::array<LowerCockpitTriangleKey, 2>{
                     {{LowerCockpitContactBuffer::halo, 0, 662},
                      {LowerCockpitContactBuffer::halo, 0, 663}}} &&
             port->faces[0].key == summary->sources[0].keys[0] &&
             port->faces[1].key == summary->sources[0].keys[1] &&
             summary->sources[0].evaluated_faces ==
                 std::array<std::optional<std::uint32_t>, 2>{182, 183} &&
             summary->sources[0].object == 10;
        break;

      case 6:
        ok = summary->sources[1].keys ==
                 std::array<LowerCockpitTriangleKey, 2>{
                     {{LowerCockpitContactBuffer::original, 0, 62119},
                      {LowerCockpitContactBuffer::original, 0, 62120}}} &&
             star->faces[0].key == summary->sources[1].keys[0] &&
             star->faces[1].key == summary->sources[1].keys[1];
        break;

      case 7:
        ok = summary->sources[0].name ==
                 "CRAFT | pilot transition intermediate step" &&
             summary->sources[0].inventory_provenance == 735 &&
             summary->sources[1].name == "CABIN | cockpit transition step" &&
             summary->sources[1].object == 124;
        break;

      case 8:
        ok = summary->sources[0].plane == double(-230000) * 1e-6 &&
             summary->sources[1].plane == double(-160000) * 1e-6 &&
             port->plane_metres == summary->sources[0].plane &&
             star->plane_metres == summary->sources[1].plane;
        break;

      case 9:
      case 10: {
        const auto& q = summary->quads[i - 9];
        ok = q.complete && q.arithmetic_supported && q.horizontal && q.upward &&
             q.convex && q.nondegenerate && q.corner_membership &&
             q.distinct_face_vertices && q.incidence_valid && q.diagonal_valid;
        break;
      }
      case 11: ok = d.load_version == 1; break;

      case 12:
        d.parts = detail::boarding_route_foot_phase_parts();
        ok = true;
        break;

      case 13:
        ok = d.parts.size() == 15;
        for (std::size_t j = 0; j < d.parts.size(); ++j)
          ok = ok && static_cast<std::size_t>(d.parts[j].id) == j;
        break;

      case 14:
      case 15: {
        const auto control =
            detail::boarding_route_intermediate_load01_controls(i - 11);
        if (control) d.controls[i - 14] = *control;
        ok = control && control->seconds_per_parameter == (i == 14 ? 12 : 2) &&
             control->port_reaction_fraction ==
                 (i == 14 ? std::array<double, 2>{0, .0625}
                          : std::array<double, 2>{.0625, .0625});
        break;
      }
      case 16:
        d.join_expression_identity =
            detail::boarding_route_intermediate_load01_join_math(d.controls[0],
                                                                 d.controls[1]);
        ok = d.join_expression_identity;
        break;

      case 17: ok = d.load_version == 1; break;

      default: break;
    }
    if (!ok)
      return fail(d, r, i == 0 ? W::invalid_binding : W::source_identity);
  }
  d.source_enrolled = true;
  return true;
}
[[gnu::noinline]] auto reset_cell(C& c, double first, double last,
                                  std::size_t phase) -> void {
  c = C{};
  c.global_first = first;
  c.global_last = last;
  c.phase_index = phase;
}
auto metadata(R& r, Pending p, std::optional<std::size_t> phase) -> void {
  r.first = p.first;
  r.last = p.last;
  r.depth = p.depth;
  r.phase_index = phase;
  if (phase) {
    r.local_first =
        detail::boarding_route_intermediate_load01_local(*phase, p.first);
    r.local_last =
        detail::boarding_route_intermediate_load01_local(*phase, p.last);
  }
}
auto terminal(D& d, Pending p, W w, std::optional<std::size_t> phase = {})
    -> void {
  R r;
  metadata(r, p, phase);
  r.condition = r.predicate_condition = w;
  d.stop_condition = w;
  d.state = capacity(w) ? S::capacity : S::unresolved;
  d.first_refusal = r;
}
auto verify(const D& d, double first, double last) -> bool {
  if (d.cells.empty()) return false;
  double cursor = first;
  for (const auto& c : d.cells) {
    const auto ph = detail::boarding_route_intermediate_load01_phase(
        c.global_first, c.global_last);
    if (c.global_first != cursor || !ph || c.phase_index != *ph ||
        c.phase.first != detail::boarding_route_intermediate_load01_local(
                             *ph, c.global_first) ||
        c.phase.last != detail::boarding_route_intermediate_load01_local(
                            *ph, c.global_last) ||
        !c.complete || !c.projection_complete || !c.nominal_equilibrium ||
        !c.finite_contact_complete || !c.nominal_load_complete)
      return false;
    cursor = c.global_last;
  }
  return cursor == last && (!(first < .75 && last > .75) || d.qualified_join);
}
[[gnu::noinline]] auto run_cover(
    const detail::BoardingRouteIntermediateLoad01AdmissionContext& context,
    D& d, Pending requested, const L& l) -> void {
  std::array<Pending, 11> pending{};
  pending[0] = requested;
  std::size_t count = 1;
  C c;

  while (count) {
    const auto p = pending[--count];

    if (d.examined_nodes >= l.phase.nodes) {
      terminal(d, p, W::node_capacity);
      return;
    }
    if (d.cells.size() >= l.phase.leaves) {
      terminal(d, p, W::leaf_capacity);
      return;
    }
    if (d.cells.size() >= d.cells.capacity()) {
      terminal(d, p, W::output_capacity);
      return;
    }
    ++d.examined_nodes;
    d.maximum_depth = std::max(d.maximum_depth, p.depth);

    if (p.first < .75 && p.last > .75) {
      if (p.depth >= l.phase.depth) {
        terminal(d, p, W::depth_capacity);
        return;
      }
      if (count + 2 > pending.size()) {
        terminal(d, p, W::unsplittable_interval);
        return;
      }
      ++d.mandatory_splits;
      pending[count++] = {.75, p.last, p.depth + 1};
      pending[count++] = {p.first, .75, p.depth + 1};
      continue;
    }
    const auto phase =
        detail::boarding_route_intermediate_load01_phase(p.first, p.last);
    if (!phase) {
      terminal(d, p, W::incomplete_cover);
      return;
    }
    reset_cell(c, p.first, p.last, *phase);
    R reason;
    metadata(reason, p, phase);
    d.stop_condition = W::none;
    d.state = S::not_run;

    const auto state = detail::boarding_route_intermediate_load01_current_cell(
        context, d, c, l, reason);

    if (state == S::accepted) {
      d.cells.push_back(c);
      if (d.cells.size() >= 2) {
        const auto& left = d.cells[d.cells.size() - 2];
        const auto& right = d.cells.back();
        if (d.join_expression_identity && requested.first < .75 &&
            requested.last > .75 && left.phase_index == 3 &&
            right.phase_index == 4 && left.global_first < .75 &&
            left.global_last == .75 && right.global_first == .75 &&
            right.global_last > .75 && left.complete && right.complete)
          d.qualified_join = true;
      }
      continue;
    }
    if (state == S::capacity || state == S::unsupported) {
      d.first_refusal = reason;
      return;
    }
    if (p.first == p.last)
      reason.condition = W::unsplittable_interval;

    else if (p.depth >= l.phase.depth)
      reason.condition = W::depth_capacity;

    else {
      const auto mid = p.first + (p.last - p.first) * .5;
      if (mid > p.first && mid < p.last && count + 2 <= pending.size()) {
        pending[count++] = {mid, p.last, p.depth + 1};
        pending[count++] = {p.first, mid, p.depth + 1};
        continue;
      }
      reason.condition = W::unsplittable_interval;
    }
    d.first_refusal = reason;
    d.stop_condition = reason.condition;
    return;
  }
  d.complete = verify(d, requested.first, requested.last);
  d.kinematic_complete = d.nonnegative_reactions = d.nominal_equilibrium =
      d.finite_contact_complete = d.nominal_load_complete = d.complete;

  d.state = d.complete ? S::accepted : S::unresolved;
  if (!d.complete) terminal(d, requested, W::incomplete_cover);
}
} // namespace
auto detail::boarding_route_intermediate_load01_definition_charge(D& d, C& c,
                                                                  const L& l,
                                                                  R& r)
    -> bool {
  if (!charge(d, r, d.work.definition_guards, l.definition_guards,
              W::definition_capacity))
    return false;

  const auto it = std::find(c.definition_evaluated.begin(),
                            c.definition_evaluated.end(), false);

  if (it == c.definition_evaluated.end())
    return fail(d, r, W::definition_capacity);
  *it = true;
  return true;
}
[[gnu::noinline]] auto detail::boarding_route_intermediate_load01_allocate(
    D& d, C& c, const L& l, R& r) -> bool {
  std::array<B, 2> com;
  std::array<std::array<B, 2>, 2> centers;

  for (std::size_t i = 0; i < 3; ++i)
    for (std::size_t a = 0; a < 2; ++a) {
      if (!guard(
              d, c, l, r,
              [&] {
                const auto& v =
                    i == 0 ? c.phase.center_of_mass.value
                           : c.phase
                                 .points[static_cast<std::size_t>(
                                     i == 1 ? P::port_boot_center
                                            : P::starboard_boot_center)]
                                 .value;

                const B b{a == 0 ? v.lower.x : v.lower.z,
                          a == 0 ? v.upper.x : v.upper.z, true};
                if (!valid(b, 8)) return false;
                (i == 0 ? com[a] : centers[i - 1][a]) = b;
                return true;
              },
              W::unsupported_arithmetic))
        return false;
    }
  const BoardingBootSourcePartition* port{};

  for (std::size_t side = 0; side < 2; ++side) {
    if (!guard(
            d, c, l, r,
            [&] {
              const auto* src = A::partition(d.source, side);
              const auto* summary = d.source.summary();
              if (!src || !summary || !summary->complete) return false;
              const auto& id = summary->sources[side];
              if (id.keys[0] != src->faces[0].key ||
                  id.keys[1] != src->faces[1].key ||
                  id.plane != src->plane_metres || id.name.empty())
                return false;
              if (side == 0) port = src;
              return true;
            },
            W::source_identity))
      return false;
  }
  std::array<B, 2> solelow, solehigh, lows, highs;
  std::array<std::array<B, 2>, 4> vertices;

  for (std::size_t a = 0; a < 2; ++a)
    for (std::size_t which = 0; which < 2; ++which) {
      if (!charge(d, r, d.work.sole_extrema, l.sole_extrema,
                  W::sole_extrema_capacity, a, which))
        return false;
      c.sole_extrema_evaluated[a][which] = true;
      (which == 0 ? solelow[a] : solehigh[a]) =
          which == 0 ? sub(centers[0][a], point(a == 0 ? .06 : .14))
                     : add(centers[0][a], point(a == 0 ? .06 : .14));
    }
  if (!guard(
          d, c, l, r,
          [&] {
            return valid(solelow[0], 16) && valid(solelow[1], 16) &&
                   valid(solehigh[0], 16) && valid(solehigh[1], 16);
          },
          W::unsupported_arithmetic))
    return false;

  for (std::size_t v = 0; v < 4; ++v)
    for (std::size_t a = 0; a < 2; ++a) {
      if (!charge(d, r, d.work.source_coordinates, l.source_coordinates,
                  W::source_coordinate_capacity, a, v))
        return false;
      c.source_coordinate_evaluated[v][a] = true;
      const auto p = port->perimeter_metres[v];
      const B b = point(a == 0 ? p.x : p.z);
      if (!valid(b, 8)) return fail(d, r, W::unsupported_arithmetic, a, v);
      vertices[v][a] = b;
      lows[a] = v == 0 ? b : minimum(lows[a], b);
      highs[a] = v == 0 ? b : maximum(highs[a], b);
    }
  if (!guard(
          d, c, l, r,
          [&] {
            return valid(lows[0], 8) && valid(lows[1], 8) &&
                   valid(highs[0], 8) && valid(highs[1], 8);
          },
          W::unsupported_arithmetic))
    return false;

  auto equal = [](B a, B b) {
    return a.lower == a.upper && b.lower == b.upper && a.lower == b.lower;
  };

  if (!guard(
          d, c, l, r,
          [&] {
            return equal(vertices[0][0], vertices[3][0]) &&
                   equal(vertices[1][0], vertices[2][0]) &&
                   equal(vertices[0][1], vertices[1][1]) &&
                   equal(vertices[2][1], vertices[3][1]);
          },
          W::source_rectangle))
    return false;

  if (!guard(
          d, c, l, r,
          [&] {
            return vertices[1][0].upper < vertices[0][0].lower &&
                   vertices[0][1].upper < vertices[2][1].lower;
          },
          W::source_rectangle))
    return false;

  const bool zero =
      c.phase_index == 3 && c.phase.first == 0 && c.phase.last == 0;

  const bool full =
      c.phase_index == 4 || (c.phase.first == 1 && c.phase.last == 1);

  if (!charge(d, r, d.work.reaction_operations, l.reaction_operations,
              W::reaction_capacity, {}, 0))
    return false;
  c.reaction_evaluated[0] = true;

  const B reported{c.phase.port_reaction_fraction.lower,
                   c.phase.port_reaction_fraction.upper, true};

  c.port_share = intersect(reported, zero   ? point(0)
                                     : full ? point(.0625)
                                            : B{0, .0625, true});

  if (!guard(
          d, c, l, r,
          [&] {
            if (!valid(c.port_share, .0625) || c.port_share.lower < 0)
              return false;

            if (c.phase_index == 3 && c.phase.first == 0 &&
                (reported.lower > 0 || reported.upper < 0))
              return false;

            if ((c.phase_index == 4 || c.phase.last == 1) &&
                (reported.lower > .0625 || reported.upper < .0625))
              return false;

            c.port_force_state = zero ? Force::zero_only
                                 : c.phase_index == 3 && c.phase.first == 0
                                     ? Force::zero_boundary_positive_interior
                                     : Force::everywhere_positive;

            c.port_positive_interior = c.port_force_state != Force::zero_only;
            c.contains_zero_endpoint = c.phase_index == 3 && c.phase.first == 0;

            c.checkpoint_threshold_met = full && .0625 > .010;
            return true;
          },
          W::reaction_identity))
    return false;

  if (!charge(d, r, d.work.reaction_operations, l.reaction_operations,
              W::reaction_capacity, {}, 1))
    return false;
  c.reaction_evaluated[1] = true;
  const auto rawcomplement = sub(point(1), c.port_share);

  if (!charge(d, r, d.work.reaction_operations, l.reaction_operations,
              W::reaction_capacity, {}, 2))
    return false;
  c.reaction_evaluated[2] = true;
  c.star_share = intersect(rawcomplement, zero   ? point(1)
                                          : full ? point(.9375)
                                                 : B{.9375, 1, true});

  if (!guard(
          d, c, l, r,
          [&] {
            if (!valid(c.star_share, 1) || c.star_share.lower < .9375)
              return false;
            if (c.contains_zero_endpoint &&
                (rawcomplement.lower > 1 || rawcomplement.upper < 1))
              return false;
            if ((c.phase_index == 4 || c.phase.last == 1) &&
                (rawcomplement.lower > .9375 || rawcomplement.upper < .9375))
              return false;
            c.star_everywhere_positive = true;
            return true;
          },
          W::denominator))
    return false;

  std::array<B, 2> lo, hi;

  for (std::size_t a = 0; a < 2; ++a) {
    for (std::size_t which = 0; which < 2; ++which) {
      if (!charge(d, r, d.work.intersection_operations,
                  l.intersection_operations, W::intersection_capacity, a,
                  which))
        return false;
      c.intersection_evaluated[a][which] = true;
      (which == 0 ? lo[a] : hi[a]) = which == 0
                                         ? maximum(solelow[a], lows[a])
                                         : minimum(solehigh[a], highs[a]);
    }
    if (!guard(
            d, c, l, r,
            [&] {
              return valid(lo[a], 32) && valid(hi[a], 32) &&
                     lo[a].upper < hi[a].lower;
            },
            W::empty_intersection))
      return false;
  }
  if (!charge(d, r, d.work.pressure_candidates, l.pressure_candidates,
              W::pressure_capacity, {}, 0))
    return false;

  for (std::size_t a = 0; a < 2; ++a) {
    if (!charge(d, r, d.work.midpoint_operations, l.midpoint_operations,
                W::midpoint_capacity, a, 0))
      return false;
    c.midpoint_evaluated[a][0] = true;
    const auto sum = add(lo[a], hi[a]);
    if (!charge(d, r, d.work.midpoint_operations, l.midpoint_operations,
                W::midpoint_capacity, a, 1))
      return false;
    c.midpoint_evaluated[a][1] = true;
    c.pressure_xz[0][a] = mul(point(.5), sum);
    if (!guard(
            d, c, l, r, [&] { return valid(c.pressure_xz[0][a], 32); },
            W::unsupported_arithmetic))
      return false;
  }
  c.pressure_evaluated[0] = true;

  if (!charge(d, r, d.work.pressure_candidates, l.pressure_candidates,
              W::pressure_capacity, {}, 1))
    return false;

  for (std::size_t a = 0; a < 2; ++a) {
    if (!charge(d, r, d.work.allocation_operations, l.allocation_operations,
                W::allocation_capacity, a, 0))
      return false;
    c.allocation_evaluated[a][0] = true;
    const auto weighted = mul(c.port_share, c.pressure_xz[0][a]);

    if (!charge(d, r, d.work.allocation_operations, l.allocation_operations,
                W::allocation_capacity, a, 1))
      return false;
    c.allocation_evaluated[a][1] = true;
    const auto residual = sub(com[a], weighted);

    if (!guard(
            d, c, l, r, [&] { return valid(residual, 32); },
            W::unsupported_arithmetic))
      return false;

    if (!charge(d, r, d.work.allocation_operations, l.allocation_operations,
                W::allocation_capacity, a, 2))
      return false;
    c.allocation_evaluated[a][2] = true;

    const auto why = divide(
        residual, c.star_share, c.pressure_xz[1][a], nullptr,
        c.division_quotient_evaluated[a], c.division_fold_evaluated[a],
        [&](std::size_t i) {
          return charge(d, r, d.work.division_quotients, l.division_quotients,
                        W::division_quotient_capacity, a, i);
        },
        [&](std::size_t i, std::size_t side) {
          return charge(d, r, d.work.division_folds, l.division_folds,
                        W::division_fold_capacity, a, 2 * i + side);
        });

    if (why != W::none) return fail(d, r, why, a);
    if (!guard(
            d, c, l, r, [&] { return valid(c.pressure_xz[1][a], 32); },
            W::unsupported_arithmetic))
      return false;
  }
  c.pressure_evaluated[1] = true;

  // Symbolic identities use the SAME narrowed genuine function and stored
  // port COP: star=(COM-w*port)/(1-w), hence force=1 and moments=COM.
  if (!guard(
          d, c, l, r,
          [&] {
            return c.reaction_evaluated ==
                       std::array<bool, 3>{true, true, true} &&
                   c.star_everywhere_positive;
          },
          W::symbolic_equilibrium))
    return false;

  if (!guard(
          d, c, l, r,
          [&] {
            return c.pressure_evaluated == std::array<bool, 2>{true, true} &&
                   std::all_of(c.allocation_evaluated.begin(),
                               c.allocation_evaluated.end(),
                               [](const auto& x) {
                                 return std::all_of(x.begin(), x.end(),
                                                    [](bool y) { return y; });
                               }) &&
                   std::all_of(c.division_quotient_evaluated.begin(),
                               c.division_quotient_evaluated.end(),
                               [](const auto& x) {
                                 return std::all_of(x.begin(), x.end(),
                                                    [](bool y) { return y; });
                               }) &&
                   std::all_of(c.division_fold_evaluated.begin(),
                               c.division_fold_evaluated.end(),
                               [](const auto& x) {
                                 return std::all_of(x.begin(), x.end(),
                                                    [](const auto& y) {
                                                      return y[0] && y[1];
                                                    });
                               });
          },
          W::symbolic_equilibrium))
    return false;

  c.nominal_equilibrium = true;
  return true;
}
[[gnu::noinline]] auto detail::boarding_route_intermediate_load01_current_cell(
    const BoardingRouteIntermediateLoad01AdmissionContext& context, D& d, C& c,
    const L& l, R& r) -> S {
  if (!guard(
          d, c, l, r,
          [&] {
            return context.owner_ == &d && A::valid(d.source) &&
                   context.data_ == A::data(d.source) &&
                   context.controls_ == &d.controls &&
                   context.parts_ == &d.parts;
          },
          W::invalid_binding))
    return d.state;

  if (!guard(
          d, c, l, r, [] { return boarding_route_foot_phase_environment(); },
          W::unsupported_arithmetic))
    return d.state;

  const Request* request{};

  if (!guard(
          d, c, l, r,
          [&] {
            const auto phase = boarding_route_intermediate_load01_phase(
                c.global_first, c.global_last);
            if (!phase || *phase != c.phase_index || *phase < 3 || *phase > 4)
              return false;
            request = &d.controls[*phase - 3];
            return request->seconds_per_parameter == (*phase == 3 ? 12 : 2);
          },
          W::phase_prerequisite))
    return d.state;

  BoardingRouteFootPhaseRefusal reason;
  ++d.work.phase_calls;

  const auto status = boarding_route_foot_phase_cell(
      *request,
      boarding_route_intermediate_load01_local(c.phase_index, c.global_first),
      boarding_route_intermediate_load01_local(c.phase_index, c.global_last),
      d.reverse, l.phase, d.work.phase, c.phase, reason);

  if (!guard(
          d, c, l, r,
          [&] { return status == BoardingRouteFootPhaseCellResult::accepted; },
          W::phase_prerequisite)) {
    r.phase = reason;
    r.limiting_bound = {reason.limiting_bound.lower,
                        reason.limiting_bound.upper, false};
    r.side = reason.side;

    if (status == BoardingRouteFootPhaseCellResult::capacity)
      d.state = S::capacity;

    if (status == BoardingRouteFootPhaseCellResult::unsupported) {
      d.state = S::unsupported;
      d.arithmetic_supported = false;
    }
    return d.state;
  }
  if (!projection(d, c, *request, l, r)) return d.state;

  const BoardingRouteIntermediateLoad01CurrentCellToken token(d, c, *request,
                                                              context);

  return boarding_route_intermediate_load01_pressure_bridge(token, d, c, l, r);
}
auto detail::boarding_route_intermediate_load01_controls(std::size_t phase)
    -> std::optional<Request> {
  auto r = boarding_route_intermediate_step02_controls(phase);
  if (r && phase == 3) r->port_reaction_fraction = {0, .0625};
  if (r && phase == 4) r->port_reaction_fraction = {.0625, .0625};
  return r;
}
auto detail::boarding_route_intermediate_load01_phase(double first, double last)
    -> std::optional<std::size_t> {
  if (!std::isfinite(first) || !std::isfinite(last) || first < .5 || last > 1 ||
      first > last || (first < .75 && last > .75))
    return {};
  return last <= .75 ? 3 : 4;
}
auto detail::boarding_route_intermediate_load01_local(std::size_t phase,
                                                      double global) -> double {
  if (phase < 3 || phase > 4 || !std::isfinite(global) || global < .5 ||
      global > 1)
    return std::numeric_limits<double>::quiet_NaN();
  return 4 * global - static_cast<double>(phase - 1);
}
auto detail::boarding_route_intermediate_load01_clock(double global) -> double {
  if (!std::isfinite(global) || global < .5 || global > 1)
    return std::numeric_limits<double>::quiet_NaN();
  return boarding_route_intermediate_step02_clock(global);
}
auto detail::boarding_route_intermediate_load01_join_math(const Request& a,
                                                          const Request& b)
    -> bool {
  return boarding_route_intermediate_step02_join_math(a, b);
}
[[gnu::noinline]] auto detail::boarding_route_intermediate_load01_bounded(
    const OriginBoardingIntermediatePauseSupport& p, double first, double last,
    L limits) -> E {
  if (!std::isfinite(first) || !std::isfinite(last) || first < .5 ||
      first > 1 || last < .5 || last > 1 || !valid_limits(limits))
    return std::unexpected(
        "Load01 requires finite [.5,1] endpoints and lowered caps");

  if (limits.phase.output_bytes < fixed_output)
    return std::unexpected("Load01 fixed two-header output capacity refused");

  E result(std::in_place, p);
  auto& d = *result;
  d.requested_first = first;
  d.requested_last = last;
  d.reverse = first > last;
  d.output_capacity_bytes = fixed_output;

  R reason;
  const Pending requested{std::min(first, last), std::max(first, last), 0};
  metadata(reason, requested, {});

  if (!enroll(d, limits, reason)) {
    d.first_refusal = reason;
    return result;
  }
  d.reporting_elapsed_seconds =
      std::abs(boarding_route_intermediate_load01_clock(last) -
               boarding_route_intermediate_load01_clock(first));

  const auto reserve =
      std::min(limits.phase.leaves,
               (limits.phase.output_bytes - fixed_output) / sizeof(C));

  try {
    d.cells.reserve(reserve);
  } catch (const std::bad_alloc&) {
    return std::unexpected("Load01 cell allocation failed");
  }
  if (d.cells.capacity() >
      (std::numeric_limits<std::size_t>::max() - fixed_output) / sizeof(C)) {
    d.output_capacity_bytes = std::numeric_limits<std::size_t>::max();
    terminal(d, requested, W::output_capacity);
    return result;
  }
  d.output_capacity_bytes = fixed_output + d.cells.capacity() * sizeof(C);

  if (d.output_capacity_bytes > limits.phase.output_bytes) {
    terminal(d, requested, W::output_capacity);
    return result;
  }
  const BoardingRouteIntermediateLoad01AdmissionContext context(d);

  run_cover(context, d, requested, limits);
  return result;
}
[[gnu::noinline]] auto detail::boarding_route_intermediate_load01_division_math(
    B n, B den, BoardingRouteIntermediateLoad01DivisionLimits limits)
    -> BoardingRouteIntermediateLoad01DivisionMath {
  BoardingRouteIntermediateLoad01DivisionMath m;
  m.numerator = n;
  m.denominator = den;

  if (limits.quotients > 4 || limits.folds > 6) {
    m.condition = W::invalid_limits;
    return m;
  }
  for (std::size_t i = 0; i < 3; ++i) {
    ++m.work.validation_guards;
    m.input_validation_evaluated[i] = true;

    const bool ok = i == 0   ? boarding_route_foot_phase_environment()
                    : i == 1 ? valid(n, 32)
                             : valid(den, 1) && den.lower > 0;

    if (!ok) {
      m.condition = i == 0 ? W::unsupported_arithmetic : W::division_input;
      return m;
    }
    if (i == 0) m.arithmetic_supported = true;
  }
  m.input_validated = true;
  m.condition = divide(
      n, den, m.bound, &m.quotients, m.quotient_evaluated, m.fold_evaluated,
      [&](std::size_t) {
        if (m.work.quotients >= limits.quotients) return false;
        ++m.work.quotients;
        return true;
      },
      [&](std::size_t, std::size_t) {
        if (m.work.folds >= limits.folds) return false;
        ++m.work.folds;
        return true;
      });

  if (m.condition == W::unsupported_arithmetic) m.arithmetic_supported = false;
  m.complete = m.condition == W::none;
  return m;
}
auto assess_origin_boarding_route_intermediate_load01(
    const OriginBoardingIntermediatePauseSupport& p, double first, double last)
    -> E {
  return detail::boarding_route_intermediate_load01_bounded(p, first, last);
}
} // namespace apsis_drift
