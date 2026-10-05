#include "apsis_drift/origin_boarding_lower_foot_transfer_surface_sweep.hpp"
#include "origin_boarding_lower_foot_transfer_surface_sweep_internal.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <utility>

namespace apsis_drift {
namespace {
using Diagnostic = BoardingLowerFootTransferSurfaceSweepDiagnostic;
using Payload = BoardingLowerFootTransferSurfaceSweepPayload;
using Coverage = BoardingLowerFootTransferSurfaceSweepCoverage;
using Condition = BoardingLowerFootTransferSurfaceSweepCondition;
using Limits = detail::BoardingLowerFootTransferSurfaceSweepLimits;
using Triangle = detail::LowerCockpitEffectiveTriangle;
using Solid = detail::BoardingSourceEndpointSurfaceCheckpointSolidBounds;
static_assert(sizeof(Payload) <=
              kBoardingLowerFootTransferSurfaceMaximumPayloadBytes);

auto valid_limits(const Limits& l) -> bool {
  const auto& c = l.child;
  return l.source_triangles <=
             kBoardingLowerFootTransferSurfaceMaximumTriangles &&
         l.base_pairs <= kBoardingLowerFootTransferSurfaceMaximumBasePairs &&
         l.refined_pairs <=
             kBoardingLowerFootTransferSurfaceMaximumRefinedPairs &&
         l.axes <= kBoardingLowerFootTransferSurfaceMaximumAxes &&
         l.pair_axes <= kBoardingLowerFootTransferSurfaceMaximumPairAxes &&
         c.source_partitions <= kBoardingBootSourcePartitionCount &&
         c.body_records <= kBoardingSourceEndpointMaximumRecords &&
         c.initial_self_pairs <= kBoardingBodyPairCount &&
         c.initial_self_axes <= kBoardingSourceEndpointSelfMaximumAxes &&
         c.initial_pressure_partitions <= kBoardingBootSourcePartitionCount &&
         c.depth <= kBoardingLowerFootTransferMaximumDepth &&
         c.nodes <= kBoardingLowerFootTransferMaximumNodes &&
         c.leaves <= kBoardingLowerFootTransferMaximumLeaves &&
         c.self_pairs <= kBoardingLowerFootTransferMaximumPairs &&
         c.proposed_axes <= kBoardingLowerFootTransferMaximumAxes &&
         c.signed_trials <= kBoardingLowerFootTransferMaximumSignedTrials &&
         c.pressure_candidates <=
             kBoardingLowerFootTransferMaximumPressureCandidates &&
         c.disk_edges <= kBoardingLowerFootTransferMaximumEdges;
}
[[gnu::noinline]] auto initial_result(const OriginBoardingBootSupport& provider,
                                      double first, double last,
                                      const Limits& limits)
    -> std::expected<Diagnostic, std::string> {
  auto child = detail::boarding_lower_foot_transfer_bounded(provider, first,
                                                            last, limits.child);
  if (!child) return std::unexpected(child.error());
  return Diagnostic{std::move(*child), {}};
}
auto refuse(Payload& out, Condition condition, BoardingBodyPartId part = {},
            const Triangle* triangle = nullptr,
            std::optional<std::uint64_t> base = {},
            std::optional<std::size_t> cell = {}, double first = 0,
            double last = 0, std::uint64_t axes = 0,
            std::uint64_t unsupported = 0) -> void {
  out.first_refusal.emplace();
  auto& r = *out.first_refusal;
  r.condition = condition;
  r.part = part;
  r.base_pair_index = base;
  r.cell_index = cell;
  r.first = first;
  r.last = last;
  r.axes_examined = axes;
  r.unsupported_axes = unsupported;
  if (triangle) {
    r.key = triangle->key;
    r.object = triangle->object;
    r.source_object = triangle->source_object;
    r.evaluated_source_triangle = triangle->evaluated_source_triangle;
    if (triangle->obstacle)
      r.actual_triangle_metres = triangle->obstacle->points;
  }
}
auto expected_counts(Payload& out, std::uint64_t parts, std::uint64_t cells,
                     const Limits& limits) -> bool {
  constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
  out.cell_count = cells;
  if (out.effective_triangle_count == 0 || cells == 0 ||
      out.effective_triangle_count > maximum / parts) {
    refuse(out, Condition::source_capacity);
    return false;
  }
  out.expected_base_pairs = out.effective_triangle_count * parts;
  if (out.expected_base_pairs > maximum / cells) {
    refuse(out, Condition::source_capacity);
    return false;
  }
  out.logical_expected_comparisons = out.expected_base_pairs * cells;
  if (out.effective_triangle_count > limits.source_triangles) {
    refuse(out, Condition::source_capacity);
    return false;
  }
  return true;
}
// Shared streamed-base decision for the source consumer and arithmetic-only
// adversaries. Coverage is the immutable UNION, never a refined Workspace.
template <class Pair>
auto certify_base(const Coverage& coverage, const Triangle& triangle,
                  std::size_t cell_count, const Limits& limits, Payload& out,
                  const Pair& pair) -> bool {
  const auto ordinal = out.examined_base_pairs;
  if (ordinal >= limits.base_pairs) {
    refuse(out, Condition::base_pair_capacity, coverage.part, &triangle,
           ordinal);
    return false;
  }
  ++out.examined_base_pairs;
  if (!triangle.obstacle) {
    out.arithmetic_supported = false;
    refuse(out, Condition::unsupported_arithmetic, coverage.part, &triangle,
           ordinal);
    return false;
  }
  const auto broad = detail::boarding_lower_foot_transfer_surface_union_broad(
      coverage, triangle.obstacle->points);
  if (!broad || !broad->arithmetic_supported) {
    out.arithmetic_supported = false;
    refuse(out, Condition::unsupported_arithmetic, coverage.part, &triangle,
           ordinal);
    return false;
  }
  if (broad->certified) {
    ++out.union_broad_certified_pairs;
    ++out.completed_base_pairs;
    out.logical_certified_comparisons += cell_count;
    return true;
  }
  for (std::size_t i = 0; i < cell_count; ++i) {
    if (out.examined_cell_pairs >= limits.refined_pairs) {
      refuse(out, Condition::refined_pair_capacity, coverage.part, &triangle,
             ordinal, i, pair.first(i), pair.last(i));
      return false;
    }
    ++out.examined_cell_pairs;
    const auto remaining = limits.axes - out.axes_examined;
    const auto allowance = static_cast<std::size_t>(
        std::min<std::uint64_t>(remaining, limits.pair_axes));
    const auto evidence = pair(i, triangle, allowance);
    if (!evidence || evidence->axes_examined > allowance ||
        evidence->unsupported_axes > evidence->axes_examined) {
      out.arithmetic_supported = false;
      refuse(out, Condition::unsupported_arithmetic, coverage.part, &triangle,
             ordinal, i, pair.first(i), pair.last(i));
      return false;
    }
    out.axes_examined += evidence->axes_examined;
    out.unsupported_axes += evidence->unsupported_axes;
    if (!evidence->certified) {
      const auto condition = evidence->truncated
                                 ? (remaining < limits.pair_axes
                                        ? Condition::aggregate_axis_capacity
                                        : Condition::pair_axis_capacity)
                             : !evidence->arithmetic_supported
                                 ? Condition::unsupported_arithmetic
                                 : Condition::no_separation_certificate;
      if (condition == Condition::unsupported_arithmetic)
        out.arithmetic_supported = false;
      refuse(out, condition, coverage.part, &triangle, ordinal, i,
             pair.first(i), pair.last(i), evidence->axes_examined,
             evidence->unsupported_axes);
      return false;
    }
    if (!evidence->arithmetic_supported) {
      out.arithmetic_supported = false;
      refuse(out, Condition::unsupported_arithmetic, coverage.part, &triangle,
             ordinal, i, pair.first(i), pair.last(i));
      return false;
    }
    ++out.certified_cell_pairs;
    ++out.logical_certified_comparisons;
    if (evidence->axes_examined == 0)
      ++out.cell_broad_certified_pairs;
    else if (evidence->sole_contact)
      ++out.cell_sole_certified_pairs;
    else
      ++out.cell_support_certified_pairs;
  }
  ++out.refined_completed_base_pairs;
  ++out.completed_base_pairs;
  return true;
}
auto comparisons_complete(const Payload& out) -> bool {
  return !out.first_refusal && out.arithmetic_supported &&
         out.examined_base_pairs == out.expected_base_pairs &&
         out.completed_base_pairs == out.expected_base_pairs &&
         out.completed_base_pairs == out.union_broad_certified_pairs +
                                         out.refined_completed_base_pairs &&
         out.logical_certified_comparisons ==
             out.logical_expected_comparisons &&
         out.examined_cell_pairs == out.certified_cell_pairs &&
         out.certified_cell_pairs == out.cell_broad_certified_pairs +
                                         out.cell_sole_certified_pairs +
                                         out.cell_support_certified_pairs;
}
struct SourcePairs {
  const detail::BoardingLowerFootTransferSurfaceContext& context;
  const BoardingLowerFootTransferDiagnostic& child;
  std::size_t part;
  auto first(std::size_t i) const -> double { return child.cells[i].first; }
  auto last(std::size_t i) const -> double { return child.cells[i].last; }
  auto operator()(std::size_t i, const Triangle& t, std::size_t axes) const {
    return detail::boarding_lower_foot_transfer_surface_cell_pair(context, part,
                                                                  i, t, axes);
  }
};
struct NumericPairs {
  std::span<const Solid> cells;
  double common_y;
  static auto first(std::size_t) -> double { return 0; }
  static auto last(std::size_t) -> double { return 0; }
  auto operator()(std::size_t i, const Triangle& t, std::size_t axes) const {
    return detail::boarding_source_endpoint_surface_checkpoint_pair_math(
        cells[i], common_y, t.obstacle->points, axes);
  }
};
auto component(RigidVector3 v, std::size_t i) -> double {
  return i == 0 ? v.x : i == 1 ? v.y : v.z;
}
auto extend(BoardingPlantedLegPointBounds& out,
            const BoardingPlantedLegPointBounds& value) -> void {
  out.lower = {std::min(out.lower.x, value.lower.x),
               std::min(out.lower.y, value.lower.y),
               std::min(out.lower.z, value.lower.z)};
  out.upper = {std::max(out.upper.x, value.upper.x),
               std::max(out.upper.y, value.upper.y),
               std::max(out.upper.z, value.upper.z)};
}
} // namespace

auto detail::boarding_lower_foot_transfer_surface_sweep_bounded(
    const OriginBoardingBootSupport& provider, double first, double last,
    Limits limits) -> std::expected<Diagnostic, std::string> {
  if (!valid_limits(limits) || !std::isfinite(first) || !std::isfinite(last) ||
      first < 0 || first > 1 || last < 0 || last > 1)
    return std::unexpected("Surface sweep requires finite [0,1] endpoints and "
                           "lowered registered caps");
  auto prepared = initial_result(provider, first, last, limits);
  if (!prepared) return prepared;
  auto& result = *prepared;
  auto& out = result.surface;
  if (!result.transfer.complete) {
    refuse(out, Condition::child_prerequisite);
    return prepared;
  }
  auto context = prepare_boarding_lower_foot_transfer_surface(result.transfer);
  if (!context) {
    refuse(out, Condition::invalid_binding);
    return prepared;
  }
  const auto* contact =
      result.transfer.initial.self.endpoint.sites.source.contact();
  if (!contact || !contact->stowed_partition())
    return std::unexpected("Surface sweep immutable selected contact required");
  {
    const auto roster = visit_effective_lower_cockpit_contact(
        *contact, [](const auto&) { return false; });
    if (!roster) return std::unexpected(roster.error());
    out.effective_triangle_count = roster->total_triangles;
    out.metadata_visited_triangles = roster->visited_triangles;
  }
  if (!expected_counts(out, kBoardingBodyPartCount,
                       result.transfer.cells.size(), limits))
    return prepared;
  out.arithmetic_supported = true;
  for (std::size_t i = 0; i < out.coverage.size(); ++i) {
    auto bounds =
        boarding_lower_foot_transfer_surface_union_bounds(*context, i);
    if (!bounds || !bounds->arithmetic_supported) {
      out.arithmetic_supported = false;
      refuse(out, Condition::unsupported_arithmetic,
             static_cast<BoardingBodyPartId>(i));
      return prepared;
    }
    out.coverage[i] = *bounds;
    const auto covered = covers_lower_cockpit_bounds(
        *contact, {bounds->lower_metres, bounds->upper_metres});
    if (!covered) return std::unexpected(covered.error());
    out.coverage[i].covered = *covered;
    if (!*covered) {
      refuse(out, Condition::crop_uncovered,
             static_cast<BoardingBodyPartId>(i));
      return prepared;
    }
  }
  out.coverage_complete = true;
  struct Scan {
    const Coverage& coverage;
    const SourcePairs& pairs;
    const Limits& limits;
    Payload& out;
    auto operator()(const Triangle& triangle) const -> bool {
      ++out.visited_triangles;
      return certify_base(coverage, triangle, pairs.child.cells.size(), limits,
                          out, pairs);
    }
  };
  static_assert(sizeof(Scan) + sizeof(SourcePairs) +
                    sizeof(std::function<bool(const Triangle&)>) +
                    sizeof(LowerCockpitEffectiveVisit) +
                    sizeof(std::reference_wrapper<const Scan>) <=
                256);
  for (std::size_t i = 0; i < out.coverage.size(); ++i) {
    const SourcePairs pairs{*context, result.transfer, i};
    const Scan scan{out.coverage[i], pairs, limits, out};
    const auto visit =
        visit_effective_lower_cockpit_contact(*contact, std::cref(scan));
    if (!visit) return std::unexpected(visit.error());
    if (visit->total_triangles != out.effective_triangle_count) {
      refuse(out, Condition::source_roster_changed,
             static_cast<BoardingBodyPartId>(i));
      return prepared;
    }
    if (!visit->complete) {
      if (!out.first_refusal)
        refuse(out, Condition::source_roster_changed,
               static_cast<BoardingBodyPartId>(i));
      return prepared;
    }
  }
  out.comparisons_complete = comparisons_complete(out);
  out.partial_continuous_surface_qualified = out.complete =
      out.coverage_complete && out.comparisons_complete;
  return prepared;
}
auto assess_origin_boarding_lower_foot_transfer_surface_sweep(
    const OriginBoardingBootSupport& provider, double first, double last)
    -> std::expected<Diagnostic, std::string> {
  return detail::boarding_lower_foot_transfer_surface_sweep_bounded(
      provider, first, last);
}
auto detail::boarding_lower_foot_transfer_surface_sweep_math(
    std::span<const Solid> cells, double common_y,
    std::span<const std::array<RigidVector3, 3>> triangles, Limits limits)
    -> std::expected<BoardingLowerFootTransferSurfaceSweepMathEvidence,
                     std::string> {
  if (!valid_limits(limits) || cells.empty() ||
      cells.size() > kBoardingLowerFootTransferMaximumLeaves ||
      triangles.empty() ||
      triangles.size() > kBoardingLowerFootTransferSurfaceMaximumTriangles)
    return std::unexpected("Local surface sweep requires nonempty bounded "
                           "cells/triangles and lowered caps");
  auto united = cells.front();
  for (const auto& cell : cells) {
    const auto cell_bounds =
        boarding_lower_foot_transfer_surface_union_bounds_math(cell, common_y);
    if (cell.shape != united.shape ||
        cell.half_size_metres != united.half_size_metres ||
        cell.radius_metres != united.radius_metres || !cell_bounds ||
        !cell_bounds->arithmetic_supported)
      return std::unexpected("Local surface cells require finite original "
                             "identical full reservations");
    extend(united.first, cell.first);
    extend(united.second, cell.second);
  }
  for (const auto& triangle : triangles)
    for (auto p : triangle)
      for (std::size_t i = 0; i < 3; ++i)
        if (!std::isfinite(component(p, i)) || std::abs(component(p, i)) > 8)
          return std::unexpected(
              "Local surface triangles require finite bounded coordinates");
  auto bounds =
      boarding_lower_foot_transfer_surface_union_bounds_math(united, common_y);
  if (!bounds) return std::unexpected(bounds.error());
  if (!bounds->arithmetic_supported)
    return std::unexpected("Local surface union requires supported arithmetic");
  BoardingLowerFootTransferSurfaceSweepMathEvidence result;
  auto& out = result.work;
  out.effective_triangle_count = triangles.size();
  if (!expected_counts(out, 1, cells.size(), limits)) return result;
  out.coverage[0] = *bounds;
  out.arithmetic_supported = true;
  const NumericPairs pairs{cells, common_y};
  for (const auto& points : triangles) {
    CabinContactObstacle obstacle{};
    obstacle.points = points;
    const Triangle triangle{.obstacle = &obstacle,
                            .key = {},
                            .object = 0,
                            .source_object = {},
                            .evaluated_source_triangle = {}};
    ++out.visited_triangles;
    if (!certify_base(out.coverage[0], triangle, cells.size(), limits, out,
                      pairs))
      return result;
  }
  out.complete = out.comparisons_complete = comparisons_complete(out);
  // Arithmetic coverage is not actual source/crop/surface admission.
  return result;
}
} // namespace apsis_drift
