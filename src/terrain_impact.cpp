#include "apsis_drift/terrain_impact.hpp"

#include "apsis_drift/godot/saved_contact_geometry.hpp"
#include "apsis_drift/rigid_frame_handoff.hpp"
#include "godot/contact_geometry_internal.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <vector>

namespace apsis_drift {
namespace {
using V = RigidVector3;
using Triangle = godot_spike::ContactTriangle;
constexpr double kClearInflation = kTerrainImpactClearMarginMetres;
constexpr double kMaximumTranslationMetres = 64.0;
constexpr double kMaximumRotationChord = 0.018;
constexpr double kContactToleranceMetres = 0.000001;
// Version1 noise is a convex interpolation of signed16 values. Its weighted
// field and sea threshold are each in [-32768,32767]; largest amplitude8500
// gives <17000m elevation. Recipe2 continuously interpolates those unchanged
// vertices and adds at most48+16+5=69m. 17070m includes binary64 rounding
// slack. Both selected contact recipes use convex top triangles of these
// vertices.
constexpr double kTerrainMaximumElevationMetres = 17'070.0;

auto add(V a, V b) -> V {
  return {a.x + b.x, a.y + b.y, a.z + b.z};
}
auto sub(V a, V b) -> V {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}
auto mul(V a, double s) -> V {
  return {a.x * s, a.y * s, a.z * s};
}
auto dot(V a, V b) -> double {
  return (a.x * b.x + a.y * b.y) + a.z * b.z;
}
auto cross(V a, V b) -> V {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
auto length(V a) -> double {
  return std::hypot(a.x, a.y, a.z);
}
auto finite(V a) -> bool {
  return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z);
}
auto rotate(RigidOrientation q, V v) -> V {
  const V imaginary{q.x, q.y, q.z};
  const double norm = ((q.w * q.w + q.x * q.x) + q.y * q.y) + q.z * q.z;
  return add(v, mul(add(mul(cross(imaginary, v), q.w),
                        cross(imaginary, cross(imaginary, v))),
                    2.0 / norm));
}
auto point(PlanetFixedPositionMetres p) -> V {
  return {p.x, p.y, p.z};
}
auto normal(const Triangle& triangle) -> V {
  return {triangle.outward_normal.x, triangle.outward_normal.y,
          triangle.outward_normal.z};
}
struct Part {
  V center, half;
  CraftImpactRegion region{};
  std::uint8_t support{};
};
struct Prepared {
  RigidBodyState start, end;
  PlanetDescriptor planet;
  std::array<V, 3> axes;
  std::array<Part, 5> parts{};
  std::size_t count{};
  V delta;
  double bound{};
  std::uint64_t source_checksum{}, candidate_checksum{};
};
auto prepare(const PhysicalLocalSystem& owner,
             const PhysicalPlanetRotationRecipe& rotation,
             const FreedomFlightSaveDocument& before,
             const FreedomFlightSaveDocument& candidate, bool gear,
             std::uint32_t policy)
    -> std::expected<std::optional<Prepared>, std::string> {
  if (!godot_spike::saved_contact_recipe(policy) ||
      before.model.terrain_policy != policy ||
      candidate.model.terrain_policy != policy)
    return std::unexpected(
        "Impact terrain selection does not match its flight owners");
  if (!validate_local_system(owner))
    return std::unexpected("Invalid impact physical owner");
  const auto old = hydrate_freedom_flight_document(before);
  const auto next = hydrate_freedom_flight_document(candidate);
  if (!old || !next)
    return std::unexpected("Invalid impact source/candidate flight document");
  if (old->system != owner || old->rotation != rotation)
    return std::unexpected(
        "Impact source catalog/rotation differs from its selected owner");
  if (before.flight.tick >= std::numeric_limits<SimulationTick>::max() - 1 ||
      candidate.flight.tick != before.flight.tick + 1)
    return std::unexpected(
        "Impact requires consecutive advanceable simulation ticks");
  if (before.origin.state.craft != candidate.origin.state.craft ||
      before.origin.lineage != candidate.origin.lineage ||
      before.origin.recipe != candidate.origin.recipe ||
      before.flight.craft != candidate.flight.craft)
    return std::unexpected("Impact cannot change craft identity or generation");
  if (next->system != owner || next->rotation != rotation ||
      before.flight.frame != candidate.flight.frame ||
      !before.flight.frame.planet ||
      (before.flight.frame.kind != RigidFrameKind::planet_relative_inertial &&
       before.flight.frame.kind != RigidFrameKind::planet_fixed) ||
      before.flight.craft !=
          CraftFrameRecipe{kWayfarerFrameId, kWayfarerFrameVersion})
    return std::optional<Prepared>{};
  const auto& context = RigidBodyWorldContext{owner};
  const RigidCoordinateFrame frame{RigidFrameKind::planet_fixed,
                                   owner.catalog.id,
                                   before.flight.frame.planet,
                                   {}};
  const auto start = reframe_rigid_body(context, before.flight,
                                        {frame, before.flight.tick}, rotation);
  const auto end = reframe_rigid_body(context, candidate.flight,
                                      {frame, candidate.flight.tick}, rotation);
  const auto selected = find_local_system_planet(owner, *frame.planet);
  const auto craft = resolve_craft_frame(before.flight.craft);
  const auto old_hash = rigid_body_state_checksum(context, before.flight);
  const auto new_hash = rigid_body_state_checksum(context, candidate.flight);
  if (!start || !end || !selected || !craft || !old_hash || !new_hash)
    return std::unexpected("Impact owned fixed-frame preparation failed");
  Prepared p{*start,
             *end,
             (*selected)->descriptor,
             {rotate(start->orientation, {1, 0, 0}),
              rotate(start->orientation, {0, 1, 0}),
              rotate(start->orientation, {0, 0, 1})},
             {},
             0,
             sub(end->position_metres, start->position_metres),
             0,
             *old_hash,
             *new_hash};
  const auto& properties = craft->properties;
  const auto component = [](const CraftPointMm& a, std::size_t axis) {
    return static_cast<double>(a[axis]) / 1000.0;
  };
  const V com{component(properties.center_of_mass_mm, 0),
              component(properties.center_of_mass_mm, 1),
              component(properties.center_of_mass_mm, 2)};
  const V low{component(properties.hull_min_mm, 0),
              component(properties.hull_min_mm, 1),
              component(properties.hull_min_mm, 2)};
  const V high{component(properties.hull_max_mm, 0),
               component(properties.hull_max_mm, 1),
               component(properties.hull_max_mm, 2)};
  p.parts[p.count++] = {sub(mul(add(low, high), .5), com),
                        mul(sub(high, low), .5), CraftImpactRegion::forebody,
                        0};
  if (gear) {
    for (std::uint8_t i = 0; i < properties.support_count; ++i) {
      const auto& pad = properties.supports[i];
      const double stroke = static_cast<double>(pad.stroke_mm) / 1000.0;
      const V center{component(pad.contact_mm, 0),
                     component(pad.contact_mm, 1) + stroke * .5,
                     component(pad.contact_mm, 2)};
      p.parts[p.count++] = {sub(center, com),
                            {static_cast<double>(pad.half_width_mm) / 1000.0,
                             stroke * .5,
                             static_cast<double>(pad.half_length_mm) / 1000.0},
                            CraftImpactRegion::landing_structure,
                            i};
    }
  }
  for (std::size_t i = 0; i < p.count; ++i)
    p.bound =
        std::max(p.bound, length(p.parts[i].center) + length(p.parts[i].half));
  return std::optional<Prepared>{std::move(p)};
}
auto shape_center(const Prepared& p, const Part& part, double fraction) -> V {
  return add(add(p.start.position_metres, mul(p.delta, fraction)),
             rotate(p.start.orientation, part.center));
}
auto radius_on(const Prepared& p, const Part& part, V axis, double inflation)
    -> double {
  return (part.half.x + inflation) * std::abs(dot(p.axes[0], axis)) +
         (part.half.y + inflation) * std::abs(dot(p.axes[1], axis)) +
         (part.half.z + inflation) * std::abs(dot(p.axes[2], axis));
}
// Continuous SAT for a fixed-orientation translating OBB and a finite triangle.
// All 13 separating axes are tested, including edge/edge axes; a plane crossing
// outside the triangle therefore cannot manufacture an impact.
auto interval(const Prepared& p, const Part& part, const Triangle& triangle,
              double inflation) -> std::optional<std::array<double, 2>> {
  const V center = shape_center(p, part, 0);
  std::array<V, 3> vertices{sub(point(triangle.vertices[0]), center),
                            sub(point(triangle.vertices[1]), center),
                            sub(point(triangle.vertices[2]), center)};
  const std::array<V, 3> edges{sub(vertices[1], vertices[0]),
                               sub(vertices[2], vertices[1]),
                               sub(vertices[0], vertices[2])};
  std::array<V, 13> axes{};
  std::ranges::copy(p.axes, axes.begin());
  axes[3] = normal(triangle);
  std::size_t index = 4;
  for (const auto box_axis : p.axes)
    for (const auto edge : edges)
      axes[index++] = cross(box_axis, edge);
  double entry = 0, exit = 1;
  for (auto axis : axes) {
    const double norm = length(axis);
    if (norm <= 1e-12) continue;
    axis = mul(axis, 1.0 / norm);
    const auto projections = std::array{
        dot(vertices[0], axis), dot(vertices[1], axis), dot(vertices[2], axis)};
    const auto [minimum, maximum] =
        std::minmax_element(projections.begin(), projections.end());
    const double radius = radius_on(p, part, axis, inflation);
    const double speed = dot(p.delta, axis);
    if (speed == 0) {
      if (*minimum > radius || *maximum < -radius) return {};
      continue;
    }
    double first = (*minimum - radius) / speed,
           last = (*maximum + radius) / speed;
    if (first > last) std::swap(first, last);
    entry = std::max(entry, first);
    exit = std::min(exit, last);
    if (entry > exit) return {};
  }
  return std::array{entry, exit};
}
// Projection SAT against the finite facet footprint. The projected OBB is a
// convex hexagon; its edge directions are the three projected box axes. The
// triangle contributes its three edges. Infinite neighboring plane extensions
// cannot establish burial when these finite projections do not overlap.
auto projection_overlap(const Prepared& p, const Part& part,
                        const Triangle& triangle) -> bool {
  const V center = shape_center(p, part, 0), n = normal(triangle);
  const std::array vertices{sub(point(triangle.vertices[0]), center),
                            sub(point(triangle.vertices[1]), center),
                            sub(point(triangle.vertices[2]), center)};
  std::array<V, 6> axes{};
  for (std::size_t i = 0; i < 3; ++i) {
    axes[i] = cross(n, p.axes[i]);
    axes[i + 3] = cross(n, sub(vertices[(i + 1) % 3], vertices[i]));
  }
  for (auto axis : axes) {
    const double norm = length(axis);
    if (norm <= 1e-12) continue;
    axis = mul(axis, 1 / norm);
    const auto projections = std::array{
        dot(vertices[0], axis), dot(vertices[1], axis), dot(vertices[2], axis)};
    const auto [minimum, maximum] =
        std::minmax_element(projections.begin(), projections.end());
    const double radius = radius_on(p, part, axis, 0);
    if (*minimum > radius || *maximum < -radius) return false;
  }
  return true;
}
// Clip the actual top triangle by all six proxy halfspaces at the SAT time.
// The centroid is on the triangle and in the proxy; numerical failure is
// unknown.
auto contact_point(const Prepared& p, const Part& part,
                   const Triangle& triangle, double fraction)
    -> std::optional<V> {
  const V center = shape_center(p, part, fraction);
  std::vector<V> polygon{point(triangle.vertices[0]),
                         point(triangle.vertices[1]),
                         point(triangle.vertices[2])};
  const std::array halves{part.half.x, part.half.y, part.half.z};
  for (std::size_t axis = 0; axis < 3; ++axis) {
    for (const double sign : {-1.0, 1.0}) {
      const V n = mul(p.axes[axis], sign);
      const auto distance = [&](V vertex) {
        return dot(sub(vertex, center), n) - halves[axis];
      };
      std::vector<V> clipped;
      if (polygon.empty()) return {};
      for (std::size_t i = 0; i < polygon.size(); ++i) {
        const V a = polygon[i], b = polygon[(i + 1) % polygon.size()];
        const double da = distance(a), db = distance(b);
        const bool inside_a = da <= kContactToleranceMetres,
                   inside_b = db <= kContactToleranceMetres;
        if (inside_a) clipped.push_back(a);
        if (inside_a != inside_b) {
          const double denominator = da - db;
          if (denominator == 0 || !std::isfinite(denominator)) return {};
          const double t = std::clamp(da / denominator, 0.0, 1.0);
          clipped.push_back(add(a, mul(sub(b, a), t)));
        }
      }
      polygon = std::move(clipped);
      if (polygon.size() > 16) return {};
    }
  }
  if (polygon.empty()) return {};
  V average{};
  for (const auto vertex : polygon)
    average = add(average, sub(vertex, center));
  const V result =
      add(center, mul(average, 1.0 / static_cast<double>(polygon.size())));
  if (!finite(result) ||
      std::abs(dot(sub(result, point(triangle.vertices[0])),
                   normal(triangle))) > kContactToleranceMetres)
    return {};
  return result;
}
struct Coverage {
  CubeFace face{};
  std::array<std::uint32_t, 2> low{262143, 262143}, high{};
};
auto coverage(const Prepared& p) -> std::optional<Coverage> {
  Coverage result;
  bool selected = false;
  for (std::size_t i = 0; i < p.count; ++i) {
    const auto& part = p.parts[i];
    for (const double fraction : {0.0, 1.0}) {
      const V center = shape_center(p, part, fraction);
      for (unsigned bits = 0; bits < 8; ++bits) {
        V corner = center;
        const std::array extents{part.half.x + kClearInflation,
                                 part.half.y + kClearInflation,
                                 part.half.z + kClearInflation};
        for (std::size_t axis = 0; axis < 3; ++axis)
          corner = add(corner, mul(p.axes[axis], (bits & (1U << axis))
                                                     ? extents[axis]
                                                     : -extents[axis]));
        const auto id = godot_spike::detail::locate_selected_contact_triangle(
            p.planet, {corner.x, corner.y, corner.z});
        if (!id) return {};
        if (!selected) {
          result.face = id->face;
          selected = true;
        }
        if (id->face != result.face) return {};
        const std::array components{corner.x, corner.y, corner.z};
        const auto primary_index = static_cast<unsigned>(result.face) / 2;
        const double primary =
            (static_cast<unsigned>(result.face) % 2 == 0 ? 1.0 : -1.0) *
            components[primary_index];
        for (std::size_t axis = 0; axis < 3; ++axis)
          if (axis != primary_index &&
              primary - std::abs(components[axis]) <= kContactToleranceMetres)
            return {};
        const std::array cell{id->tile_x * 32 + id->cell_x,
                              id->tile_y * 32 + id->cell_y};
        for (std::size_t axis = 0; axis < 2; ++axis) {
          result.low[axis] = std::min(result.low[axis], cell[axis]);
          result.high[axis] = std::max(result.high[axis], cell[axis]);
        }
      }
    }
  }
  for (std::size_t axis = 0; axis < 2; ++axis) {
    if (result.low[axis]) --result.low[axis];
    result.high[axis] = std::min(result.high[axis] + 1, 262143U);
  }
  const auto count =
      static_cast<std::uint64_t>(result.high[0] - result.low[0] + 1) *
      static_cast<std::uint64_t>(result.high[1] - result.low[1] + 1);
  if (count > kTerrainImpactMaximumCells) return {};
  return result;
}
auto material_supported(const Prepared& p, const Triangle& triangle) -> bool {
  const auto anchor = point(triangle.vertices[0]);
  const auto radial = mul(anchor, 1.0 / length(anchor));
  const double radius = p.planet.radius.value * 1000.0;
  return std::ranges::all_of(triangle.vertices, [&](auto vertex) {
    return dot(radial, point(vertex)) > radius + kContactToleranceMetres;
  });
}
auto witness(const Prepared& p, const FreedomFlightSaveDocument& before,
             std::uint32_t policy, const Part& part, const Triangle& triangle,
             double fraction, V contact) -> TerrainImpactWitness {
  const RigidOrientation inverse{p.start.orientation.w, -p.start.orientation.x,
                                 -p.start.orientation.y,
                                 -p.start.orientation.z};
  const V r = rotate(inverse, sub(contact, add(p.start.position_metres,
                                               mul(p.delta, fraction))));
  const auto craft = resolve_craft_frame(before.flight.craft);
  const auto& com = craft->properties.center_of_mass_mm;
  const V local = add(r, {com[0] / 1000.0, com[1] / 1000.0, com[2] / 1000.0});
  const V n = normal(triangle);
  const V velocity =
      add(p.end.linear_velocity_metres_per_second,
          rotate(p.start.orientation,
                 cross(p.end.angular_velocity_radians_per_second, r)));
  const double closing = std::max(0.0, -dot(velocity, n));
  return {1,
          policy,
          1,
          before.origin.state.craft,
          before.flight.craft,
          p.start.frame.system,
          *p.start.frame.planet,
          p.end.tick,
          p.source_checksum,
          p.candidate_checksum,
          fraction,
          triangle.id,
          part.region,
          part.support,
          local,
          n,
          velocity,
          closing};
}
} // namespace

auto assess_terrain_impact(const PhysicalLocalSystem& owner,
                           const PhysicalPlanetRotationRecipe& rotation,
                           const FreedomFlightSaveDocument& before,
                           const FreedomFlightSaveDocument& candidate,
                           bool gear, std::uint32_t policy)
    -> std::expected<TerrainImpactSweep, std::string> {
  const auto prepared =
      prepare(owner, rotation, before, candidate, gear, policy);
  if (!prepared) return std::unexpected{prepared.error()};
  TerrainImpactSweep result;
  if (!*prepared) return result;
  const auto& p = **prepared;
  // A sphere enclosing every proxy also encloses every orientation. This
  // conservative far-field proof requires no sampled terrain or camera guard.
  const double squared = dot(p.delta, p.delta);
  const double t =
      squared > 0 ? std::clamp(-dot(p.start.position_metres, p.delta) / squared,
                               0.0, 1.0)
                  : 0;
  // Rigid positions can reach1e15m. Closest-point cancellation must not turn
  // metre-scale uncertainty into a claimed5cm clear margin on a passing sweep.
  const double arithmetic_allowance =
      64.0 * std::numeric_limits<double>::epsilon() *
      (length(p.start.position_metres) + length(p.delta) +
       p.planet.radius.value * 1000.0);
  const double clearance =
      length(add(p.start.position_metres, mul(p.delta, t))) - p.bound -
      p.planet.radius.value * 1000.0 - kTerrainMaximumElevationMetres -
      arithmetic_allowance;
  if (clearance > kClearInflation) {
    result.classification = TerrainImpactClass::clear;
    result.minimum_clearance_metres = clearance;
    return result;
  }
  const auto& a = p.start.orientation;
  const auto& b = p.end.orientation;
  const double plus = std::hypot(std::hypot(a.w - b.w, a.x - b.x),
                                 std::hypot(a.y - b.y, a.z - b.z));
  const double minus = std::hypot(std::hypot(a.w + b.w, a.x + b.x),
                                  std::hypot(a.y + b.y, a.z + b.z));
  if (length(p.delta) > kMaximumTranslationMetres ||
      std::min(plus, minus) > kMaximumRotationChord ||
      length(p.start.angular_velocity_radians_per_second) > 2.0 ||
      length(p.end.angular_velocity_radians_per_second) > 2.0)
    return result;
  const auto cells = coverage(p);
  if (!cells) return result;
  const auto recipe = *godot_spike::saved_contact_recipe(policy);
  auto cache = TerrainTileCache::create(16);
  if (!cache) return std::unexpected("Impact macro cache unavailable");
  std::optional<TerrainImpactWitness> first;
  bool expanded_clear = true, below_unknown = false, unqualified = false;
  for (std::uint32_t x = cells->low[0]; x <= cells->high[0]; ++x) {
    for (std::uint32_t y = cells->low[1]; y <= cells->high[1]; ++y) {
      for (unsigned half = 0; half < 2; ++half) {
        const auto triangle =
            godot_spike::detail::build_selected_contact_triangle(
                owner, p.planet, recipe,
                {p.planet.id, cells->face, x / 32, y / 32, x % 32, y % 32,
                 half},
                *cache);
        if (!triangle) return result;
        for (std::size_t index = 0; index < p.count; ++index) {
          const auto& part = p.parts[index];
          if (interval(p, part, *triangle, kClearInflation))
            expanded_clear = false;
          const auto hit = interval(p, part, *triangle, 0);
          if (!hit) {
            const double top_gap =
                dot(sub(shape_center(p, part, 0), point(triangle->vertices[0])),
                    normal(*triangle)) +
                radius_on(p, part, normal(*triangle), 0);
            // Only a wholly buried proxy with a finite overlapping facet
            // footprint is ambiguous. A lower corner below an unrelated plane
            // extension is not evidence of missing contact geometry.
            if (top_gap < -kContactToleranceMetres &&
                projection_overlap(p, part, *triangle))
              below_unknown = true;
            continue;
          }
          if (!material_supported(p, *triangle)) {
            unqualified = true;
            continue;
          }
          const auto contact = contact_point(p, part, *triangle, (*hit)[0]);
          if (!contact) {
            unqualified = true;
            continue;
          }
          const auto event =
              witness(p, before, policy, part, *triangle, (*hit)[0], *contact);
          // Stable source enumeration (x,y,half,part) breaks exact-time ties.
          if (!first || event.fraction < first->fraction) first = event;
        }
      }
    }
  }
  // Unknown earlier geometry/material cannot establish a certified earliest
  // contact. Do not let a later convenient triangle conceal that uncertainty.
  if (unqualified) return result;
  if (first) {
    result.classification = TerrainImpactClass::impact;
    result.witness = first;
  } else if (expanded_clear && !below_unknown) {
    result.classification = TerrainImpactClass::clear;
    result.minimum_clearance_metres = kClearInflation;
  } else if (!below_unknown) {
    result.classification = TerrainImpactClass::near;
  }
  return result;
}

auto respond_terrain_impact(const PhysicalLocalSystem& owner,
                            const PhysicalPlanetRotationRecipe& rotation,
                            const FreedomFlightSaveDocument& before,
                            const FreedomFlightSaveDocument& candidate,
                            const TerrainImpactWitness& event)
    -> std::expected<RigidBodyState, std::string> {
  const bool gear = event.region == CraftImpactRegion::landing_structure;
  const auto prepared =
      prepare(owner, rotation, before, candidate, gear, event.terrain_policy);
  if (!prepared || !*prepared)
    return std::unexpected("Impact response source/ownership unavailable");
  const auto& p = **prepared;
  if (event.version != 1 || event.collider_policy != 1 ||
      event.craft != before.origin.state.craft ||
      event.craft_frame != before.flight.craft ||
      event.system != p.start.frame.system ||
      event.planet != *p.start.frame.planet || event.tick != p.end.tick ||
      event.source_checksum != p.source_checksum ||
      event.candidate_checksum != p.candidate_checksum ||
      !std::isfinite(event.fraction) || event.fraction < 0 ||
      event.fraction > 1 || !finite(event.contact_point_craft_metres) ||
      !finite(event.outward_normal_planet_fixed) ||
      !finite(event.pre_response_velocity_planet_fixed) ||
      !std::isfinite(event.closing_speed_metres_per_second) ||
      event.closing_speed_metres_per_second <= 0 ||
      (event.region != CraftImpactRegion::forebody &&
       event.region != CraftImpactRegion::landing_structure) ||
      (event.region == CraftImpactRegion::forebody &&
       event.support_index != 0) ||
      (gear && event.support_index >= p.count - 1))
    return std::unexpected(
        "Invalid, stale or non-inward impact response witness");
  const auto recipe = *godot_spike::saved_contact_recipe(event.terrain_policy);
  auto cache = TerrainTileCache::create(1);
  const auto triangle = godot_spike::detail::build_selected_contact_triangle(
      owner, p.planet, recipe, event.triangle, *cache);
  if (!triangle) return std::unexpected("Impact response triangle unavailable");
  const auto& part = p.parts[gear ? event.support_index + 1 : 0];
  const auto hit = interval(p, part, *triangle, 0);
  const auto contact = contact_point(p, part, *triangle, event.fraction);
  if (!hit || (*hit)[0] != event.fraction || !contact ||
      witness(p, before, event.terrain_policy, part, *triangle, event.fraction,
              *contact) != event)
    return std::unexpected(
        "Impact response witness differs from actual proxy contact");
  auto fixed = p.end;
  fixed.position_metres =
      add(add(p.start.position_metres, mul(p.delta, event.fraction)),
          mul(event.outward_normal_planet_fixed,
              kTerrainImpactResponseRetreatMetres));
  fixed.orientation = p.start.orientation;
  const double inward = dot(fixed.linear_velocity_metres_per_second,
                            event.outward_normal_planet_fixed);
  if (inward < 0)
    fixed.linear_velocity_metres_per_second =
        sub(fixed.linear_velocity_metres_per_second,
            mul(event.outward_normal_planet_fixed, inward));
  fixed.angular_velocity_radians_per_second = {};
  const RigidBodyWorldContext context{owner};
  const auto canonical = canonicalize_rigid_body_state(context, fixed);
  if (!canonical)
    return std::unexpected("Impact response fixed pose is invalid");
  const auto reframed = reframe_rigid_body(
      context, *canonical, {candidate.flight.frame, candidate.flight.tick},
      rotation);
  if (!reframed)
    return std::unexpected("Impact response native reframe failed");
  return *reframed;
}
} // namespace apsis_drift
