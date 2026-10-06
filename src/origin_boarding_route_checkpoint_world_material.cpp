#include "apsis_drift/origin_boarding_route_checkpoint_world_material.hpp"
#include "origin_boarding_checkpoint_material_extension_internal.hpp"
#include "origin_boarding_route_checkpoint_world_material02_internal.hpp"
#include "origin_boarding_route_checkpoint_world_material_internal.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
namespace apsis_drift {
namespace {
using Bounds = BoardingPlantedLegPointBounds;
using Vec = RigidVector3;
using Solid = detail::BoardingSourceEndpointSurfaceCheckpointSolidBounds;
using Proxy = detail::BoardingRouteCheckpointWorldProxy;
using Limits = detail::BoardingRouteCheckpointWorldLimits;
using Work = BoardingRouteCheckpointWorldCounters;
using Condition = BoardingRouteCheckpointWorldCondition;
using Diagnostic = BoardingRouteCheckpointWorldMaterialDiagnostic;
using Access = detail::BoardingRouteCheckpointWorldMaterialAccess;
using Context = detail::BoardingRouteCheckpointWorldContext;
using Relation = BoardingInitialMaterialRelation;
using BoundaryLimits = detail::BoardingRouteCheckpointBoundaryLimits;
using BoundaryWork = BoardingRouteCheckpointBoundaryCounters;
using Scalar = BoardingFootSiteScalarBounds;
auto axis(Vec p, std::size_t a) -> double {
  return a == 0 ? p.x : a == 1 ? p.y : p.z;
}
auto set_axis(Vec& p, std::size_t a, double x) -> void {
  if (a == 0)
    p.x = x;
  else if (a == 1)
    p.y = x;
  else
    p.z = x;
}
auto finite(Vec p) -> bool {
  return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
}
auto valid(Bounds p) -> bool {
  return finite(p.lower) && finite(p.upper) && p.lower.x <= p.upper.x &&
         p.lower.y <= p.upper.y && p.lower.z <= p.upper.z;
}
auto unite(Bounds a, Bounds b) -> Bounds {
  for (std::size_t i = 0; i < 3; ++i) {
    set_axis(a.lower, i, std::min(axis(a.lower, i), axis(b.lower, i)));
    set_axis(a.upper, i, std::max(axis(a.upper, i), axis(b.upper, i)));
  }
  return a;
}
auto separated(Bounds a, Bounds b, bool boundary_allowed = false) -> bool {
  for (std::size_t i = 0; i < 3; ++i) {
    if (boundary_allowed ? (axis(a.upper, i) <= axis(b.lower, i) ||
                            axis(b.upper, i) <= axis(a.lower, i))
                         : (axis(a.upper, i) < axis(b.lower, i) ||
                            axis(b.upper, i) < axis(a.lower, i)))
      return true;
  }
  return false;
}
auto triangle_bounds(const std::array<Vec, 3>& triangle) -> Bounds {
  Bounds out{triangle[0], triangle[0]};
  for (std::size_t i = 1; i < triangle.size(); ++i)
    out = unite(out, {triangle[i], triangle[i]});
  return out;
}
auto valid_limits(const Limits& c, const Limits& o = {}) -> bool {
  const auto& a = c.child;
  const auto& b = o.child;
  return a.phase.depth <= b.phase.depth && a.phase.nodes <= b.phase.nodes &&
         a.phase.leaves <= b.phase.leaves &&
         a.phase.output_bytes <= b.phase.output_bytes &&
         a.phase.graphs <= b.phase.graphs && a.phase.legs <= b.phase.legs &&
         a.phase.bodies <= b.phase.bodies &&
         a.phase.sectors <= b.phase.sectors &&
         a.phase.timing <= b.phase.timing &&
         a.initial_source_partitions <= b.initial_source_partitions &&
         a.initial_body_records <= b.initial_body_records &&
         a.initial_self_pairs <= b.initial_self_pairs &&
         a.initial_self_axes <= b.initial_self_axes &&
         a.initial_pressure_partitions <= b.initial_pressure_partitions &&
         a.output_bytes <= b.output_bytes && a.pairs <= b.pairs &&
         a.axes <= b.axes && a.signed_trials <= b.signed_trials &&
         a.owners <= b.owners && a.candidates <= b.candidates &&
         a.edges <= b.edges &&
         a.hip_complement_attempts <= b.hip_complement_attempts &&
         c.output_bytes <= o.output_bytes && c.pair_axes <= o.pair_axes &&
         c.roster_entries <= o.roster_entries &&
         c.effective_sources <= o.effective_sources &&
         c.union_envelope_pairs <= o.union_envelope_pairs &&
         c.union_proxy_preparations <= o.union_proxy_preparations &&
         c.domain_union_checks <= o.domain_union_checks &&
         c.domain_cell_checks <= o.domain_cell_checks &&
         c.proxy_preparations <= o.proxy_preparations &&
         c.refined_envelope_pairs <= o.refined_envelope_pairs &&
         c.material_triangle_visits <= o.material_triangle_visits &&
         c.halo_metadata_visits <= o.halo_metadata_visits &&
         c.halo_triangle_visits <= o.halo_triangle_visits &&
         c.triangle_union_pairs <= o.triangle_union_pairs &&
         c.refined_triangle_pairs <= o.refined_triangle_pairs &&
         c.base_enclosure_relations <= o.base_enclosure_relations &&
         c.refined_enclosure_relations <= o.refined_enclosure_relations &&
         c.axes <= o.axes &&
         c.direction_entries_prepared <= o.direction_entries_prepared &&
         c.sole_vertex_guards <= o.sole_vertex_guards;
}
auto charge(std::uint64_t& value, std::uint64_t cap) -> bool {
  if (value >= cap) return false;
  ++value;
  return true;
}
auto boundary_limits_valid(const BoundaryLimits& c) -> bool {
  static constexpr BoundaryLimits maximum;
  return c.witness_attempts <= maximum.witness_attempts &&
         c.signed_support_calls <= maximum.signed_support_calls &&
         c.width_attempts <= maximum.width_attempts;
}
auto reserve_boundary_support(BoundaryWork& work, const BoundaryLimits& caps)
    -> Condition {
  if (!charge(work.witness_attempts, caps.witness_attempts))
    return Condition::boundary_witness_capacity;
  if (work.signed_support_calls > caps.signed_support_calls ||
      caps.signed_support_calls - work.signed_support_calls < 2)
    return Condition::boundary_support_capacity;
  work.signed_support_calls += 2;
  return Condition::none;
}
auto boundary_witness(Scalar positive, Scalar negative, Bounds envelope,
                      const BoundaryLimits& caps, BoundaryWork& work)
    -> Condition {
  const auto supported = [](Scalar s) {
    return s.supported && std::isfinite(s.lower) && std::isfinite(s.upper) &&
           s.lower <= s.upper;
  };
  if (!supported(positive) || !supported(negative) || !valid(envelope))
    return Condition::unsupported_arithmetic;
  if (positive.lower > envelope.upper.z || negative.lower > -envelope.lower.z)
    return Condition::none;
  if (!charge(work.width_attempts, caps.width_attempts))
    return Condition::boundary_width_capacity;
  const auto width_lower =
      std::nextafter(positive.lower + negative.lower,
                     -std::numeric_limits<double>::infinity());
  const auto slab_upper =
      std::nextafter(envelope.upper.z - envelope.lower.z,
                     std::numeric_limits<double>::infinity());
  if (!std::isfinite(width_lower) || !std::isfinite(slab_upper))
    return Condition::unsupported_arithmetic;
  return width_lower > slab_upper ? Condition::none
                                  : Condition::boundary_witness_unresolved;
}
auto refuse(Diagnostic& out, Condition why,
            std::optional<std::size_t> part = {},
            std::optional<std::size_t> source = {},
            std::optional<std::size_t> cell = {}, std::string_view name = {},
            Relation relation = {}, std::optional<std::uint32_t> triangle = {},
            std::optional<LowerCockpitTriangleKey> key = {}) -> void {
  if (out.world.first_refusal) return;
  out.world.complete = false;
  out.world.first_refusal.emplace();
  auto& r = *out.world.first_refusal;
  r.condition = why;
  r.part = part;
  r.source = source;
  r.cell = cell;
  r.source_object = name;
  r.relation = relation;
  r.triangle = triangle;
  r.source_key = key;
  if (cell && out.child && *cell < out.child->cells.size()) {
    const auto& c = out.child->cells[*cell];
    r.global_first = c.global_first;
    r.global_last = c.global_last;
    r.phase_index = c.phase_index;
  }
}
// The same closed-domain fallback serves real source capabilities and the
// arithmetic-only fixture. The immutable swept union is never overwritten.
template <class CellBounds, class Failure>
auto domain_assess(const OriginLowerCockpitContact& contact,
                   Bounds trajectory_union, std::size_t cells,
                   const Limits& limits, Work& work, CellBounds get,
                   Failure fail, bool& union_complete) -> bool {
  union_complete = false;
  if (!charge(work.domain_union_checks, limits.domain_union_checks)) {
    fail(Condition::domain_capacity, {});
    return false;
  }
  auto covered = detail::covers_lower_cockpit_bounds(
      contact, {trajectory_union.lower, trajectory_union.upper});
  if (!covered) {
    fail(Condition::invalid_binding, {});
    return false;
  }
  if (*covered) {
    union_complete = true;
    return true;
  }
  for (std::size_t cell = 0; cell < cells; ++cell) {
    if (!charge(work.domain_cell_checks, limits.domain_cell_checks)) {
      fail(Condition::domain_capacity, cell);
      return false;
    }
    auto b = get(cell);
    if (!b) return false;
    covered =
        detail::covers_lower_cockpit_bounds(contact, {b->lower, b->upper});
    if (!covered) {
      fail(Condition::invalid_binding, cell);
      return false;
    }
    if (!*covered) {
      fail(Condition::domain_uncovered, cell);
      return false;
    }
  }
  return true;
}
// One pair against one immutable union, then precisely its retained cells.
// Proposals/axes are separate work. A lowered preparation cap conservatively
// refuses before an eager-list call, including its possible early broad path.
template <class CellProxy, class PairKernel, class Failure>
auto triangle_assess(const std::array<Vec, 3>& triangle,
                     const Bounds& trajectory_union, std::size_t cells,
                     const Limits& limits, Work& work, CellProxy get,
                     PairKernel pair_kernel, Failure fail,
                     bool allow_sole = true, bool strict_boundary = false)
    -> bool {
  if (!charge(work.triangle_union_pairs, limits.triangle_union_pairs)) {
    fail(Condition::pair_capacity, {});
    return false;
  }
  if (separated(trajectory_union, triangle_bounds(triangle), !strict_boundary))
    return true;
  for (std::size_t cell = 0; cell < cells; ++cell) {
    if (!charge(work.refined_triangle_pairs, limits.refined_triangle_pairs)) {
      fail(Condition::pair_capacity, cell);
      return false;
    }
    auto p = get(cell);
    if (!p) return false;
    if (!p->arithmetic_supported) {
      fail(Condition::unsupported_arithmetic, cell);
      return false;
    }
    if (allow_sole && p->sole_expression_identity &&
        std::ranges::all_of(triangle,
                            [&](Vec v) { return v.y <= p->sole_plane; })) {
      ++work.sole_triangle_exclusions;
      continue;
    }
    const auto remaining = limits.axes - work.axes_examined;
    const auto allowance = static_cast<std::size_t>(
        std::min<std::uint64_t>(remaining, limits.pair_axes));
    if (allowance == 0) {
      fail(Condition::axis_capacity, cell);
      return false;
    }
    if (limits.direction_entries_prepared - work.direction_entries_prepared <
        16) {
      fail(Condition::preparation_capacity, cell);
      return false;
    }
    auto proof = pair_kernel(p->solid, triangle, allowance);
    if (!proof || proof->axes_examined > allowance) {
      fail(Condition::unsupported_arithmetic, cell);
      return false;
    }
    work.axes_examined += proof->axes_examined;
    if (proof->axes_examined != 0) work.direction_entries_prepared += 16;
    if (!proof->arithmetic_supported) {
      fail(Condition::unsupported_arithmetic, cell);
      return false;
    }
    if (!proof->certified ||
        (strict_boundary && (!proof->certificate_gap.supported ||
                             !std::isfinite(proof->certificate_gap.lower) ||
                             proof->certificate_gap.lower <= 0))) {
      fail(proof->truncated ? Condition::axis_capacity
                            : Condition::sheet_unresolved,
           cell);
      return false;
    }
  }
  return true;
}
struct WorldStage {
  Diagnostic& result;
  const Context& context;
  const Limits& limits;
  const OriginBoardingCheckpointMaterialExtension* extension{};
  BoundaryWork* boundary_work{};
  const BoundaryLimits* boundary_limits{};
  using ExtensionAccess = detail::BoardingCheckpointMaterialExtensionAccess;
  using ExtensionRelation = detail::BoardingCheckpointMaterialExtensionRelation;
  auto extension_relation(std::size_t source) const -> ExtensionRelation {
    return extension
               ? ExtensionAccess::relation(*extension, result.source, source)
               : ExtensionRelation::none;
  }
  auto encloser_count(std::size_t source) const -> std::size_t {
    return extension_relation(source) == ExtensionRelation::frame_annulus
               ? 8
               : Access::encloser_count(context, source);
  }
  auto triangle_count(std::size_t source) const -> std::size_t {
    if (extension_relation(source) == ExtensionRelation::retained_cut_skin ||
        extension_relation(source) ==
            ExtensionRelation::closed_frame_boundary) {
      const auto* mesh =
          ExtensionAccess::mesh(*extension, result.source, source);
      return mesh ? mesh->triangles.size() : 0;
    }
    return Access::triangle_count(context, source);
  }
  auto triangle(std::size_t source, std::size_t ordinal, bool& collapsed) const
      -> std::expected<std::array<Vec, 3>, std::string> {
    return (extension_relation(source) ==
                ExtensionRelation::retained_cut_skin ||
            extension_relation(source) ==
                ExtensionRelation::closed_frame_boundary)
               ? ExtensionAccess::triangle(*extension, result.source, source,
                                           ordinal, collapsed)
               : Access::triangle(context, source, ordinal, collapsed);
  }
  auto boundary_exterior(std::size_t source, std::size_t part, std::size_t cell,
                         Bounds envelope,
                         const detail::MaterialSourceRecord& record) -> bool {
    if (!boundary_work || !boundary_limits) {
      refuse(result, Condition::invalid_binding, part, source, cell,
             record.name, record.relation);
      return false;
    }
    auto condition = reserve_boundary_support(*boundary_work, *boundary_limits);
    if (condition == Condition::none) {
      const auto support =
          detail::boarding_route_checkpoint_world_signed_z_support(context,
                                                                   part, cell);
      if (!support || !support->arithmetic_supported ||
          !support->original_world_identity)
        condition = Condition::unsupported_arithmetic;
      else
        condition =
            boundary_witness(support->positive, support->negative, envelope,
                             *boundary_limits, *boundary_work);
    }
    if (condition == Condition::none) return true;
    if (condition == Condition::unsupported_arithmetic)
      result.world.arithmetic_supported = false;
    refuse(result, condition, part, source, cell, record.name, record.relation);
    return false;
  }
  auto proxy(std::size_t part, std::size_t cell) const
      -> std::expected<Proxy, std::string> {
    auto& work = result.world.work;
    if (!charge(work.proxy_preparations, limits.proxy_preparations))
      return std::unexpected("WORLD proxy capacity");
    return detail::boarding_route_checkpoint_world_proxy(context, part, cell);
  }
  auto source_triangle(const std::array<Vec, 3>& triangle, std::size_t part,
                       std::optional<std::size_t> source, std::string_view name,
                       Relation relation, std::uint32_t ordinal,
                       std::optional<LowerCockpitTriangleKey> key = {},
                       bool allow_sole = true) const -> bool {
    auto failure = [&](Condition why, std::optional<std::size_t> cell) {
      if (why == Condition::unsupported_arithmetic)
        result.world.arithmetic_supported = false;
      refuse(result, why, part, source, cell, name, relation, ordinal, key);
    };
    auto get = [&](std::size_t cell) -> std::expected<Proxy, std::string> {
      const bool capacity =
          result.world.work.proxy_preparations >= limits.proxy_preparations;
      auto p = proxy(part, cell);
      if (!p || !p->arithmetic_supported || !p->original_world_identity) {
        failure(capacity ? Condition::proxy_capacity
                         : Condition::unsupported_arithmetic,
                cell);
        return std::unexpected(
            "WORLD triangle genuine current-cell proxy required");
      }
      return p;
    };
    auto kernel = [](const Solid& solid, const std::array<Vec, 3>& t,
                     std::size_t axes) {
      return detail::boarding_checkpoint_world_finite_triangle_pair_math(
          solid, 0, t, axes);
    };
    return triangle_assess(triangle, result.world.parts[part].trajectory_union,
                           result.child->cells.size(), limits,
                           result.world.work, get, kernel, failure, allow_sole,
                           source &&
                               extension_relation(*source) ==
                                   ExtensionRelation::closed_frame_boundary);
  }
  auto enclosure(std::size_t source, std::size_t part, std::size_t cell,
                 std::size_t ordinal,
                 const detail::MaterialSourceRecord& r) const -> bool {
    auto& w = result.world.work;
    if (!charge(w.refined_enclosure_relations,
                limits.refined_enclosure_relations)) {
      refuse(result, Condition::pair_capacity, part, source, cell, r.name,
             r.relation);
      return false;
    }
    const bool capacity = w.proxy_preparations >= limits.proxy_preparations;
    auto p = proxy(part, cell);
    if (!p || !p->arithmetic_supported || !p->original_world_identity) {
      if (!capacity) result.world.arithmetic_supported = false;
      refuse(result,
             capacity ? Condition::proxy_capacity
                      : Condition::unsupported_arithmetic,
             part, source, cell, r.name, r.relation);
      return false;
    }
    if (extension_relation(source) == ExtensionRelation::none &&
        p->sole_expression_identity &&
        Access::sole_volume_clear(context, part, source)) {
      ++w.sole_volume_exclusions;
      return true;
    }
    const auto allowance = static_cast<std::size_t>(std::min<std::uint64_t>(
        limits.pair_axes, limits.axes - w.axes_examined));
    if (allowance == 0) {
      refuse(result, Condition::axis_capacity, part, source, cell, r.name,
             r.relation);
      return false;
    }
    const bool eager = r.relation == Relation::service_enclosure;
    if (eager &&
        limits.direction_entries_prepared - w.direction_entries_prepared < 16) {
      refuse(result, Condition::preparation_capacity, part, source, cell,
             r.name, r.relation);
      return false;
    }
    // Every authentic safe capsule call prepares all sixteen entries before
    // trial/projection, including a subsequent arithmetic refusal.
    auto proof =
        extension_relation(source) == ExtensionRelation::frame_annulus
            ? detail::initial_material_primitive_math(
                  p->solid, 0,
                  ExtensionAccess::planes(*extension, result.source, source,
                                          ordinal),
                  allowance)
            : Access::enclosure(context, source, ordinal, *p, allowance);
    if (eager && proof && proof->axes_examined != 0)
      w.direction_entries_prepared += 16;
    if (!proof || proof->axes_examined > allowance) {
      result.world.arithmetic_supported = false;
      refuse(result, Condition::unsupported_arithmetic, part, source, cell,
             r.name, r.relation);
      return false;
    }
    w.axes_examined += proof->axes_examined;
    if (!proof->arithmetic_supported) {
      result.world.arithmetic_supported = false;
      refuse(result, Condition::unsupported_arithmetic, part, source, cell,
             r.name, r.relation);
      return false;
    }
    if (!proof->certified) {
      const auto required = eager ? 16U : 6U;
      refuse(result,
             allowance < required ? Condition::axis_capacity
                                  : Condition::enclosure_unresolved,
             part, source, cell, r.name, r.relation);
      return false;
    }
    return true;
  }
};
auto prepare_unions(WorldStage& stage) -> bool {
  auto& out = stage.result;
  const auto* contact =
      out.child->initial->self.endpoint.sites.source.contact();
  if (!contact || out.child->cells.size() > 1024 ||
      !Access::valid(stage.context)) {
    refuse(out, Condition::invalid_binding);
    return false;
  }
  for (std::size_t part = 0; part < 15; ++part) {
    auto& summary = out.world.parts[part];
    summary.id = static_cast<BoardingBodyPartId>(part);
    for (std::size_t cell = 0; cell < out.child->cells.size(); ++cell) {
      if (!charge(out.world.work.union_proxy_preparations,
                  stage.limits.union_proxy_preparations)) {
        refuse(out, Condition::proxy_capacity, part, {}, cell);
        return false;
      }
      const bool capacity =
          out.world.work.proxy_preparations >= stage.limits.proxy_preparations;
      auto proxy = stage.proxy(part, cell);
      if (!proxy || !proxy->arithmetic_supported ||
          !proxy->original_world_identity || !valid(proxy->bounds)) {
        if (!capacity) out.world.arithmetic_supported = false;
        refuse(out,
               capacity ? Condition::proxy_capacity
                        : Condition::unsupported_arithmetic,
               part, {}, cell);
        return false;
      }
      summary.trajectory_union =
          cell == 0 ? proxy->bounds
                    : unite(summary.trajectory_union, proxy->bounds);
    }
    summary.original_world_identity = true;
    auto get = [&](std::size_t cell) -> std::expected<Bounds, std::string> {
      const bool capacity =
          out.world.work.proxy_preparations >= stage.limits.proxy_preparations;
      auto p = stage.proxy(part, cell);
      if (!p || !p->arithmetic_supported || !p->original_world_identity) {
        if (!capacity) out.world.arithmetic_supported = false;
        refuse(out,
               capacity ? Condition::proxy_capacity
                        : Condition::unsupported_arithmetic,
               part, {}, cell);
        return std::unexpected("WORLD domain genuine cell proxy required");
      }
      return p->bounds;
    };
    auto failure = [&](Condition why, std::optional<std::size_t> cell) {
      refuse(out, why, part, {}, cell);
    };
    if (!domain_assess(*contact, summary.trajectory_union,
                       out.child->cells.size(), stage.limits, out.world.work,
                       get, failure, summary.union_domain_complete))
      return false;
    summary.domain_complete = true;
  }
  out.world.domain_complete = true;
  return true;
}
auto material_assess(WorldStage& stage) -> bool {
  auto& out = stage.result;
  auto& w = out.world.work;
  for (std::size_t source = 0; source < Access::source_count(stage.context);
       ++source) {
    if (!charge(w.roster_entries, stage.limits.roster_entries)) {
      refuse(out, Condition::roster_capacity, {}, source, {},
             out.source.source_name(source));
      return false;
    }
    const auto* r = Access::source(stage.context, source);
    if (!r) {
      refuse(out, Condition::invalid_binding, {}, source);
      return false;
    }
    if (r->removed) continue;
    if (!charge(w.effective_sources, stage.limits.effective_sources)) {
      refuse(out, Condition::roster_capacity, {}, source, {}, r->name,
             r->relation);
      return false;
    }
    auto envelope = Access::envelope(stage.context, source);
    if (!envelope || !valid(*envelope)) {
      refuse(out, Condition::source_identity, {}, source, {}, r->name,
             r->relation);
      return false;
    }
    std::array<bool, 15> pending{};
    for (std::size_t part = 0; part < 15; ++part) {
      if (!charge(w.union_envelope_pairs, stage.limits.union_envelope_pairs)) {
        refuse(out, Condition::envelope_capacity, part, source, {}, r->name,
               r->relation);
        return false;
      }
      auto& summary = out.world.parts[part];
      if (separated(summary.trajectory_union, *envelope)) {
        ++summary.material_sources_closed;
        summary.material_cells_closed += out.child->cells.size();
        continue;
      }
      const auto enclosers = stage.encloser_count(source);
      const auto added = stage.extension_relation(source);
      const bool frame = added == WorldStage::ExtensionRelation::frame_annulus;
      const bool boundary =
          added == WorldStage::ExtensionRelation::closed_frame_boundary;
      const bool sheet =
          r->relation == Relation::shell_sheet ||
          added == WorldStage::ExtensionRelation::retained_cut_skin;
      if (r->relation == Relation::service_enclosure ||
          r->relation == Relation::support_enclosure || frame) {
        if (enclosers == 0) {
          refuse(out, Condition::source_identity, part, source, {}, r->name,
                 r->relation);
          return false;
        }
        for (std::size_t n = 0; n < enclosers; ++n)
          if (!charge(w.base_enclosure_relations,
                      stage.limits.base_enclosure_relations)) {
            refuse(out, Condition::pair_capacity, part, source, {}, r->name,
                   r->relation);
            return false;
          }
      }
      for (std::size_t cell = 0; cell < out.child->cells.size(); ++cell) {
        if (!charge(w.refined_envelope_pairs,
                    stage.limits.refined_envelope_pairs)) {
          refuse(out, Condition::envelope_capacity, part, source, cell, r->name,
                 r->relation);
          return false;
        }
        const bool capacity =
            w.proxy_preparations >= stage.limits.proxy_preparations;
        auto p = stage.proxy(part, cell);
        if (!p || !p->arithmetic_supported || !p->original_world_identity) {
          if (!capacity) out.world.arithmetic_supported = false;
          refuse(out,
                 capacity ? Condition::proxy_capacity
                          : Condition::unsupported_arithmetic,
                 part, source, cell, r->name, r->relation);
          return false;
        }
        if (separated(p->bounds, *envelope)) continue;
        if (!frame && !sheet && !boundary &&
            (r->relation == Relation::unknown ||
             r->relation == Relation::stowed)) {
          refuse(out, Condition::missing_relation, part, source, cell, r->name,
                 r->relation);
          return false;
        }
        if (boundary &&
            !stage.boundary_exterior(source, part, cell, *envelope, *r))
          return false;
        if (sheet || boundary) {
          pending[part] = true;
          continue;
        }
        for (std::size_t n = 0; n < enclosers; ++n)
          if (!stage.enclosure(source, part, cell, n, *r)) return false;
      }
      if (!pending[part]) {
        ++summary.material_sources_closed;
        summary.material_cells_closed += out.child->cells.size();
      }
    }
    if (!std::ranges::any_of(pending, [](bool p) { return p; })) continue;
    const auto count = stage.triangle_count(source);
    if (count == 0) {
      refuse(out, Condition::source_identity, {}, source, {}, r->name,
             r->relation);
      return false;
    }
    for (std::size_t ordinal = 0; ordinal < count; ++ordinal) {
      if (!charge(w.material_triangle_visits,
                  stage.limits.material_triangle_visits)) {
        refuse(out, Condition::triangle_capacity, {}, source, {}, r->name,
               r->relation, static_cast<std::uint32_t>(ordinal));
        return false;
      }
      bool collapsed{};
      auto triangle = stage.triangle(source, ordinal, collapsed);
      if (!triangle) {
        refuse(out, Condition::source_identity, {}, source, {}, r->name,
               r->relation, static_cast<std::uint32_t>(ordinal));
        return false;
      }
      if (collapsed) {
        ++w.collapsed_triangles;
        continue;
      }
      for (std::size_t part = 0; part < 15; ++part)
        if (pending[part]) {
          if (!stage.source_triangle(*triangle, part, source, r->name,
                                     r->relation,
                                     static_cast<std::uint32_t>(ordinal), {},
                                     stage.extension_relation(source) ==
                                         WorldStage::ExtensionRelation::none))
            return false;
          ++out.world.parts[part].material_triangles_closed;
        }
    }
    for (std::size_t part = 0; part < 15; ++part)
      if (pending[part]) {
        ++out.world.parts[part].material_sources_closed;
        out.world.parts[part].material_cells_closed += out.child->cells.size();
      }
  }
  if (w.roster_entries != 1759 || w.effective_sources != 1751) {
    refuse(out, Condition::source_identity);
    return false;
  }
  for (const auto& p : out.world.parts)
    if (p.material_sources_closed != 1751 ||
        p.material_cells_closed != 1751 * out.child->cells.size()) {
      refuse(out, Condition::source_identity, static_cast<std::size_t>(p.id));
      return false;
    }
  out.world.material_exclusion = true;
  return true;
}
auto halo_callback(void* cookie,
                   const detail::LowerCockpitEffectiveTriangle& triangle)
    -> bool {
  auto& stage = *static_cast<WorldStage*>(cookie);
  auto& out = stage.result;
  if (!triangle.obstacle ||
      triangle.key.buffer != LowerCockpitContactBuffer::halo ||
      !std::ranges::all_of(triangle.obstacle->points,
                           [](Vec p) { return finite(p); })) {
    refuse(out, Condition::source_identity, {}, {}, {}, triangle.source_object,
           {}, triangle.key.triangle, triangle.key);
    return false;
  }
  ++out.world.work.halo_triangle_visits;
  for (std::size_t part = 0; part < 15; ++part) {
    if (!stage.source_triangle(triangle.obstacle->points, part, {},
                               triangle.source_object, Relation::shell_sheet,
                               triangle.key.triangle, triangle.key))
      return false;
    ++out.world.parts[part].halo_triangles_closed;
  }
  return true;
}
auto halo_assess(WorldStage& stage) -> bool {
  auto& out = stage.result;
  const auto* contact =
      out.child->initial->self.endpoint.sites.source.contact();
  if (!contact) {
    refuse(out, Condition::invalid_binding);
    return false;
  }
  auto visited = detail::visit_lower_cockpit_halo(
      *contact, &stage, halo_callback,
      static_cast<std::size_t>(stage.limits.halo_metadata_visits),
      static_cast<std::size_t>(stage.limits.halo_triangle_visits));
  if (!visited) {
    refuse(out, Condition::source_identity);
    return false;
  }
  out.world.work.halo_metadata_visits = visited->metadata_examined;
  if (visited->visited_triangles != out.world.work.halo_triangle_visits) {
    refuse(out, Condition::source_identity);
    return false;
  }
  if (!visited->complete) {
    refuse(out,
           visited->condition ==
                   detail::LowerCockpitHaloVisitCondition::metadata_capacity
               ? Condition::roster_capacity
               : Condition::triangle_capacity,
           {}, {}, {}, {}, Relation::shell_sheet, {}, visited->next_key);
    return false;
  }
  if (visited->total_metadata != 75 || visited->total_triangles != 8100 ||
      !visited->metadata_complete) {
    refuse(out, Condition::source_identity);
    return false;
  }
  for (const auto& p : out.world.parts)
    if (p.halo_triangles_closed != 8100) {
      refuse(out, Condition::source_identity, static_cast<std::size_t>(p.id));
      return false;
    }
  out.world.halo_surface_exclusion = true;
  return true;
}
auto numeric_proxy(const Solid& solid) -> std::expected<Proxy, std::string> {
  Proxy out;
  out.solid = solid;
  for (std::size_t a = 0; a < 3; ++a) {
    Vec n{};
    set_axis(n, a, 1);
    auto h = detail::boarding_source_endpoint_surface_checkpoint_support_math(
        solid, 0, n);
    set_axis(n, a, -1);
    auto negative =
        detail::boarding_source_endpoint_surface_checkpoint_support_math(solid,
                                                                         0, n);
    if (!h || !negative)
      return std::unexpected("WORLD numeric bounded solid required");
    if (!h->supported || !negative->supported) return out;
    set_axis(out.bounds.lower, a, -negative->upper);
    set_axis(out.bounds.upper, a, h->upper);
  }
  out.arithmetic_supported = valid(out.bounds);
  return out;
}
constexpr std::size_t world_fixed_output =
    sizeof(std::expected<Diagnostic, std::string>) +
    sizeof(BoardingRouteCheckpointUnloadSelf02Diagnostic) +
    sizeof(BoardingSourceEndpointLoadDiagnostic);
auto owned_output_bytes(const Diagnostic& result) -> std::size_t {
  auto bytes = sizeof(std::expected<Diagnostic, std::string>);
  if (result.child) {
    bytes += sizeof(BoardingRouteCheckpointUnloadSelf02Diagnostic);
    if (result.child->initial)
      bytes += sizeof(BoardingSourceEndpointLoadDiagnostic);
    bytes += result.child->cells.capacity() *
             sizeof(BoardingRouteCheckpointUnloadSelf02Cell);
  }
  return bytes;
}
static_assert(sizeof(BoardingRouteCheckpointWorldPayload) <= 4096);
static_assert(
    world_fixed_output +
        sizeof(std::expected<BoardingRouteCheckpointWorldMaterial02Diagnostic,
                             std::string>) -
        sizeof(std::expected<Diagnostic, std::string>) +
        1024 * sizeof(BoardingRouteCheckpointUnloadSelf02Cell) <=
    kBoardingRouteCheckpointWorldMaximumOutputBytes);
// Full new expected output/control is resident; child cells/source are borrowed
// prior owned output, never copied. Two helper reserves deliberately overlap
// conservatively: proxy4096 and capsule+old finite support6144. The extra pool
// covers union scan, callbacks, outgoing temporaries and new error characters.
constexpr std::size_t world_live_bound =
    sizeof(std::expected<BoardingRouteCheckpointWorldMaterial02Diagnostic,
                         std::string>) +
    2 * sizeof(Limits) + sizeof(std::expected<void, std::string>) +
    sizeof(std::expected<Context, std::string>) + sizeof(WorldStage) +
    4 * sizeof(std::expected<Proxy, std::string>) + 15 * sizeof(Bounds) + 4096 +
    6144 + 1024 + 512 + 2 * sizeof(BoundaryLimits) +
    sizeof(std::expected<detail::BoardingRouteCheckpointWorldSignedZSupport,
                         std::string>);
static_assert(world_live_bound <= 16384);
// Existing Self02 source-live43456 already includes its3392B expected return.
// No fresh graph/cell is live in the outer wrapper. All persistent new storage,
// return staging and a2048B controller/error reserve are charged additionally.
constexpr std::size_t world_child_live_bound =
    43456 +
    sizeof(std::expected<BoardingRouteCheckpointWorldMaterial02Diagnostic,
                         std::string>) +
    2 * sizeof(Limits) + sizeof(std::expected<void, std::string>) +
    sizeof(std::expected<Context, std::string>) +
    sizeof(std::expected<
           std::unique_ptr<BoardingRouteCheckpointUnloadSelf02Diagnostic>,
           std::string>) +
    2048 + 2 * sizeof(BoundaryLimits);
static_assert(world_child_live_bound <=
              kBoardingRouteCheckpointUnloadMaximumScratchBytes);
} // namespace
namespace detail {
auto boarding_route_checkpoint_world_limits_valid(const Limits& limits)
    -> bool {
  return valid_limits(limits);
}
auto run_checkpoint_world(
    const OriginBoardingBootSupport& boot,
    const OriginBoardingInitialMaterial& material, double first, double last,
    const Limits& limits, Diagnostic& result,
    const OriginBoardingCheckpointMaterialExtension* extension = nullptr,
    std::size_t extra_output = 0, BoundaryWork* boundary_work = nullptr,
    const BoundaryLimits* boundary_limits = nullptr)
    -> std::expected<void, std::string> {
  if (extension)
    result.world.world_version = kBoardingRouteCheckpointWorldMaterial02Version;
  if (!valid_limits(limits,
                    extension
                        ? boarding_route_checkpoint_world_material02_limits()
                        : Limits{}) ||
      !std::isfinite(first) || !std::isfinite(last) || first < 0 || first > 1 ||
      last < 0 || last > 1)
    return std::unexpected(
        "WORLD lowered registered capacities and endpoints in[0,1] required");
  result.world.output_capacity_bytes =
      owned_output_bytes(result) + extra_output;
  if (world_fixed_output + extra_output > limits.output_bytes) {
    refuse(result, Condition::output_capacity);
    return {};
  }
  auto preparation_limits = limits;
  static constexpr Limits base_limits;
  preparation_limits.proxy_preparations =
      std::min(limits.proxy_preparations, base_limits.proxy_preparations);
  preparation_limits.material_triangle_visits = std::min(
      limits.material_triangle_visits, base_limits.material_triangle_visits);
  preparation_limits.triangle_union_pairs =
      std::min(limits.triangle_union_pairs, base_limits.triangle_union_pairs);
  preparation_limits.base_enclosure_relations = std::min(
      limits.base_enclosure_relations, base_limits.base_enclosure_relations);
  preparation_limits.refined_enclosure_relations =
      std::min(limits.refined_enclosure_relations,
               base_limits.refined_enclosure_relations);
  preparation_limits.output_bytes -= extra_output;
  auto prepared = prepare_boarding_route_checkpoint_world(
      boot, material, first, last, preparation_limits, result);
  if (extension)
    result.world.world_version = kBoardingRouteCheckpointWorldMaterial02Version;
  if (!prepared) {
    result.world.output_capacity_bytes =
        owned_output_bytes(result) + extra_output;
    if (!result.world.first_refusal) return std::unexpected(prepared.error());
    return {};
  }
  const auto capacity = result.child->cells.capacity();
  result.world.output_capacity_bytes =
      owned_output_bytes(result) + extra_output;
  if (capacity > (limits.output_bytes - world_fixed_output - extra_output) /
                     sizeof(BoardingRouteCheckpointUnloadSelf02Cell)) {
    refuse(result, Condition::output_capacity);
    return {};
  }
  result.world.output_capacity_bytes =
      owned_output_bytes(result) + extra_output;
  WorldStage stage{result,    *prepared,     limits,
                   extension, boundary_work, boundary_limits};
  if (!prepare_unions(stage) || !material_assess(stage) || !halo_assess(stage))
    return {};
  for (auto& p : result.world.parts)
    p.complete = p.original_world_identity && p.domain_complete &&
                 p.material_sources_closed == 1751 &&
                 p.halo_triangles_closed == 8100;
  result.world.complete =
      !result.world.first_refusal && result.world.bindings_complete &&
      result.world.source_complete && result.world.arithmetic_supported &&
      result.world.domain_complete && result.world.material_exclusion &&
      result.world.halo_surface_exclusion &&
      std::ranges::all_of(result.world.parts,
                          [](const auto& p) { return p.complete; });
  return {};
}
auto boarding_route_checkpoint_world_bounded(
    const OriginBoardingBootSupport& boot,
    const OriginBoardingInitialMaterial& material, double first, double last,
    Limits limits) -> std::expected<Diagnostic, std::string> {
  Diagnostic result(material);
  auto ran = run_checkpoint_world(boot, material, first, last, limits, result);
  if (!ran) return std::unexpected(ran.error());
  return result;
}
auto boarding_route_checkpoint_world_material02_limits() -> Limits {
  Limits out;
  out.base_enclosure_relations = 360;
  out.refined_enclosure_relations = 368640;
  out.proxy_preparations = 2496512;
  out.material_triangle_visits = 354786;
  out.triangle_union_pairs = 5443290;
  return out;
}
auto boarding_route_checkpoint_world_material02_bounded(
    const OriginBoardingBootSupport& boot,
    const OriginBoardingInitialMaterial& material,
    const OriginBoardingCheckpointMaterialExtension& extension, double first,
    double last, Limits limits, BoundaryLimits boundary_limits)
    -> std::expected<BoardingRouteCheckpointWorldMaterial02Diagnostic,
                     std::string> {
  if (!BoardingCheckpointMaterialExtensionAccess::valid(extension, material))
    return std::unexpected(
        "WORLD02 authenticated extension for same immutable base required");
  if (!boundary_limits_valid(boundary_limits))
    return std::unexpected("WORLD02 lowered boundary capacities required");
  BoardingRouteCheckpointWorldMaterial02Diagnostic out(material, extension);
  constexpr auto extra =
      sizeof(std::expected<BoardingRouteCheckpointWorldMaterial02Diagnostic,
                           std::string>) -
      sizeof(std::expected<Diagnostic, std::string>);
  auto ran = run_checkpoint_world(boot, material, first, last, limits,
                                  out.result, &out.extension, extra,
                                  &out.boundary_work, &boundary_limits);
  if (!ran) return std::unexpected(ran.error());
  return out;
}
auto boarding_route_checkpoint_boundary_witness_math(Scalar positive,
                                                     Scalar negative,
                                                     Bounds envelope,
                                                     BoundaryLimits caps)
    -> std::expected<BoardingRouteCheckpointBoundaryWitnessMath, std::string> {
  if (!boundary_limits_valid(caps))
    return std::unexpected("Boundary witness lowered capacities required");
  BoardingRouteCheckpointBoundaryWitnessMath out;
  out.arithmetic_supported = boarding_route_foot_phase_environment();
  if (!out.arithmetic_supported) {
    out.condition = Condition::unsupported_arithmetic;
    return out;
  }
  out.condition = reserve_boundary_support(out.work, caps);
  if (out.condition == Condition::none)
    out.condition =
        boundary_witness(positive, negative, envelope, caps, out.work);
  if (out.condition == Condition::unsupported_arithmetic)
    out.arithmetic_supported = false;
  out.certified = out.condition == Condition::none;
  return out;
}
auto boarding_route_checkpoint_world_sweep_math(
    std::span<const Solid> cells, std::span<const std::array<Vec, 3>> triangles,
    Limits limits, bool strict_boundary)
    -> std::expected<BoardingRouteCheckpointWorldSweepMath, std::string> {
  if (!valid_limits(limits) || cells.empty() || cells.size() > 8 ||
      triangles.empty() || triangles.size() > 8)
    return std::unexpected("WORLD numeric sweep one..eight cells/triangles and "
                           "lowered caps required");
  for (const auto& triangle : triangles)
    for (const auto p : triangle)
      if (!finite(p) || std::abs(p.x) > 8 || std::abs(p.y) > 8 ||
          std::abs(p.z) > 8)
        return std::unexpected(
            "WORLD numeric bounded finite triangles required");
  BoardingRouteCheckpointWorldSweepMath result;
  result.arithmetic_supported = boarding_route_foot_phase_environment();
  if (!result.arithmetic_supported) {
    result.condition = Condition::unsupported_arithmetic;
    return result;
  }
  Bounds trajectory_union{};
  for (std::size_t cell = 0; cell < cells.size(); ++cell) {
    if (!charge(result.work.union_proxy_preparations,
                limits.union_proxy_preparations) ||
        !charge(result.work.proxy_preparations, limits.proxy_preparations)) {
      result.condition = Condition::proxy_capacity;
      result.refused_cell = cell;
      return result;
    }
    auto p = numeric_proxy(cells[cell]);
    if (!p) return std::unexpected(p.error());
    if (!p->arithmetic_supported) {
      result.arithmetic_supported = false;
      result.condition = Condition::unsupported_arithmetic;
      result.refused_cell = cell;
      return result;
    }
    trajectory_union =
        cell == 0 ? p->bounds : unite(trajectory_union, p->bounds);
  }
  for (std::size_t t = 0; t < triangles.size(); ++t) {
    auto fail = [&](Condition why, std::optional<std::size_t> cell) {
      result.condition = why;
      result.refused_triangle = t;
      result.refused_cell = cell;
      if (why == Condition::unsupported_arithmetic)
        result.arithmetic_supported = false;
    };
    auto get = [&](std::size_t cell) -> std::expected<Proxy, std::string> {
      if (!charge(result.work.proxy_preparations, limits.proxy_preparations)) {
        fail(Condition::proxy_capacity, cell);
        return std::unexpected("WORLD numeric proxy capacity");
      }
      auto p = numeric_proxy(cells[cell]);
      if (!p || !p->arithmetic_supported)
        fail(Condition::unsupported_arithmetic, cell);
      return p;
    };
    auto pair = [](const Solid& solid, const std::array<Vec, 3>& triangle,
                   std::size_t cap) {
      return boarding_source_endpoint_surface_checkpoint_pair_math(
          solid, 0, triangle, cap);
    };
    if (!triangle_assess(triangles[t], trajectory_union, cells.size(), limits,
                         result.work, get, pair, fail, !strict_boundary,
                         strict_boundary))
      return result;
  }
  result.complete = true;
  return result;
}
auto boarding_route_checkpoint_world_domain_math(
    const OriginLowerCockpitContact& contact, std::span<const Bounds> cells,
    Limits limits)
    -> std::expected<BoardingRouteCheckpointWorldSweepMath, std::string> {
  if (!valid_limits(limits) || cells.empty() || cells.size() > 8 ||
      !std::ranges::all_of(cells, [](Bounds b) { return valid(b); }))
    return std::unexpected(
        "WORLD numeric domain one..eight finite ordered bounds required");
  BoardingRouteCheckpointWorldSweepMath out;
  out.arithmetic_supported = boarding_route_foot_phase_environment();
  if (!out.arithmetic_supported) {
    out.condition = Condition::unsupported_arithmetic;
    return out;
  }
  Bounds trajectory_union = cells[0];
  for (std::size_t i = 1; i < cells.size(); ++i)
    trajectory_union = unite(trajectory_union, cells[i]);
  auto get = [&](std::size_t cell) -> std::expected<Bounds, std::string> {
    return cells[cell];
  };
  auto failure = [&](Condition why, std::optional<std::size_t> cell) {
    out.condition = why;
    out.refused_cell = cell;
  };
  bool union_complete{};
  out.complete = domain_assess(contact, trajectory_union, cells.size(), limits,
                               out.work, get, failure, union_complete);
  return out;
}
} // namespace detail
auto assess_origin_boarding_route_checkpoint_world_material(
    const OriginBoardingBootSupport& boot,
    const OriginBoardingInitialMaterial& material, double first, double last)
    -> std::expected<Diagnostic, std::string> {
  return detail::boarding_route_checkpoint_world_bounded(boot, material, first,
                                                         last, {});
}
auto assess_origin_boarding_route_checkpoint_world_material02(
    const OriginBoardingBootSupport& boot,
    const OriginBoardingInitialMaterial& material,
    const OriginBoardingCheckpointMaterialExtension& extension, double first,
    double last)
    -> std::expected<BoardingRouteCheckpointWorldMaterial02Diagnostic,
                     std::string> {
  return detail::boarding_route_checkpoint_world_material02_bounded(
      boot, material, extension, first, last,
      detail::boarding_route_checkpoint_world_material02_limits());
}
} // namespace apsis_drift
