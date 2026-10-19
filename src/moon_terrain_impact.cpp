#include "apsis_drift/moon_terrain_impact.hpp"

#include "apsis_drift/rigid_frame_handoff.hpp"
#include "terrain_impact_internal.hpp"

#include <limits>

namespace apsis_drift {
namespace {
using namespace detail::impact_numeric;
constexpr double kClearInflation = kMoonTerrainImpactClearMarginMetres;
constexpr double kMaximumTranslationMetres = 64.0;
constexpr double kMaximumRotationChord = 0.018;
struct Part {
  V center, half;
  MoonCraftImpactRegion region{};
  std::uint8_t support{};
};
struct Prepared {
  RigidBodyState start, end;
  std::array<V, 3> axes;
  std::array<Part, 5> parts{};
  std::size_t count{};
  V delta;
  double bound{};
  std::uint64_t source_checksum{}, candidate_checksum{};
};
auto prepare(const PhysicalMoonDescriptor& owner, const RigidBodyState& before,
             const RigidBodyState& candidate, bool gear, std::uint32_t version)
    -> std::expected<Prepared, std::string> {
  if (version != kMoonTerrainRecipeVersion ||
      !validate_physical_moon_descriptor(owner))
    return std::unexpected("Invalid moon impact owner or terrain version");
  const RigidBodyWorldContext context{owner};
  if (!validate_rigid_body_state(context, before) ||
      !validate_rigid_body_state(context, candidate))
    return std::unexpected("Invalid moon impact source/candidate state");
  if (before.tick >= std::numeric_limits<SimulationTick>::max() - 1 ||
      candidate.tick != before.tick + 1)
    return std::unexpected(
        "Moon impact requires consecutive advanceable ticks");
  if (before.craft != candidate.craft ||
      before.craft !=
          CraftFrameRecipe{kWayfarerFrameId, kWayfarerFrameVersion} ||
      before.frame != candidate.frame || before.frame.body != owner.body ||
      (before.frame.kind != RigidFrameKind::body_relative_inertial &&
       before.frame.kind != RigidFrameKind::body_fixed))
    return std::unexpected(
        "Moon impact requires the same typed moon and craft");
  const RigidCoordinateFrame frame{
      RigidFrameKind::body_fixed, context.system.id, {}, {}, owner.body};
  const auto start =
      reframe_rigid_body(context, before, {frame, before.tick}, owner);
  const auto end =
      reframe_rigid_body(context, candidate, {frame, candidate.tick}, owner);
  const auto craft = resolve_craft_frame(before.craft);
  const auto old_hash = rigid_body_state_checksum(context, before);
  const auto new_hash = rigid_body_state_checksum(context, candidate);
  if (!start || !end || !craft || !old_hash || !new_hash)
    return std::unexpected("Moon impact owned fixed-frame preparation failed");
  Prepared p{*start,
             *end,
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
                        mul(sub(high, low), .5),
                        MoonCraftImpactRegion::forebody, 0};
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
                            MoonCraftImpactRegion::landing_structure,
                            i};
    }
  }
  for (std::size_t i = 0; i < p.count; ++i)
    p.bound =
        std::max(p.bound, length(p.parts[i].center) + length(p.parts[i].half));
  return p;
}
struct Coverage {
  CubeFace face{};
  std::array<std::uint32_t, 2> low{262143, 262143}, high{};
};
auto coverage(const Prepared& p, const MoonTerrainSampler& sampler)
    -> std::optional<Coverage> {
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
        const auto id = locate_moon_contact_triangle(
            sampler, {corner.x, corner.y, corner.z});
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
  if (count > kMoonTerrainImpactMaximumCells) return {};
  return result;
}
auto witness(const Prepared& p, std::uint32_t version, const Part& part,
             const MoonContactTriangle& triangle, double fraction, V contact)
    -> MoonTerrainImpactWitness {
  const RigidOrientation inverse{p.start.orientation.w, -p.start.orientation.x,
                                 -p.start.orientation.y,
                                 -p.start.orientation.z};
  const V r = rotate(inverse, sub(contact, add(p.start.position_metres,
                                               mul(p.delta, fraction))));
  const auto craft = resolve_craft_frame(p.start.craft);
  const auto& com = craft->properties.center_of_mass_mm;
  const V local = add(r, {com[0] / 1000.0, com[1] / 1000.0, com[2] / 1000.0});
  const V n = normal(triangle);
  const V velocity =
      add(p.end.linear_velocity_metres_per_second,
          rotate(p.start.orientation,
                 cross(p.end.angular_velocity_radians_per_second, r)));
  const double closing = std::max(0.0, -dot(velocity, n));
  return {1,
          version,
          1,
          p.start.craft,
          p.start.frame.system,
          *p.start.frame.body,
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

auto assess_moon_terrain_impact(const PhysicalMoonDescriptor& owner,
                                const RigidBodyState& before,
                                const RigidBodyState& candidate, bool gear,
                                std::uint32_t version)
    -> std::expected<MoonTerrainImpactSweep, std::string> {
  const auto prepared = prepare(owner, before, candidate, gear, version);
  if (!prepared) return std::unexpected{prepared.error()};
  const auto& p = *prepared;
  MoonTerrainImpactSweep result;
  const double squared = dot(p.delta, p.delta);
  const double t =
      squared > 0 ? std::clamp(-dot(p.start.position_metres, p.delta) / squared,
                               0.0, 1.0)
                  : 0;
  const double arithmetic_allowance =
      64.0 * std::numeric_limits<double>::epsilon() *
      (length(p.start.position_metres) + length(p.delta) + owner.radius_metres);
  // Actual recipe1's absolute relief envelope, plus1m rounding slack. The
  // enclosing sphere is valid at every attitude, even outside near-field caps.
  const double clearance =
      length(add(p.start.position_metres, mul(p.delta, t))) - p.bound -
      owner.radius_metres - kMoonTerrainMaximumReliefMetres - 1.0 -
      arithmetic_allowance;
  if (clearance > kClearInflation) {
    result.classification = MoonTerrainImpactClass::clear;
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
  const auto sampler = MoonTerrainSampler::create(owner, version);
  if (!sampler)
    return std::unexpected("Moon impact terrain source unavailable");
  const auto cells = coverage(p, *sampler);
  if (!cells) return result;
  std::optional<MoonTerrainImpactWitness> first;
  bool expanded_clear = true, below_unknown = false, unqualified = false;
  for (std::uint32_t x = cells->low[0]; x <= cells->high[0]; ++x) {
    for (std::uint32_t y = cells->low[1]; y <= cells->high[1]; ++y) {
      for (unsigned half = 0; half < 2; ++half) {
        const auto triangle = build_moon_contact_triangle(
            *sampler,
            {owner.body, cells->face, x / 32, y / 32, x % 32, y % 32, half});
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
            if (top_gap < -kContactToleranceMetres &&
                projection_overlap(p, part, *triangle))
              below_unknown = true;
            continue;
          }
          // Every authenticated moon facet is dry, including negative basins.
          const auto contact = contact_point(p, part, *triangle, (*hit)[0]);
          if (!contact) {
            unqualified = true;
            continue;
          }
          const auto event =
              witness(p, version, part, *triangle, (*hit)[0], *contact);
          if (!first || event.fraction < first->fraction) first = event;
        }
      }
    }
  }
  if (unqualified) return result;
  if (first) {
    result.classification = MoonTerrainImpactClass::impact;
    result.witness = first;
  } else if (expanded_clear && !below_unknown) {
    result.classification = MoonTerrainImpactClass::clear;
    result.minimum_clearance_metres = kClearInflation;
  } else if (!below_unknown) {
    result.classification = MoonTerrainImpactClass::near;
  }
  return result;
}

auto respond_moon_terrain_impact(const PhysicalMoonDescriptor& owner,
                                 const RigidBodyState& before,
                                 const RigidBodyState& candidate,
                                 const MoonTerrainImpactWitness& event)
    -> std::expected<RigidBodyState, std::string> {
  const bool gear = event.region == MoonCraftImpactRegion::landing_structure;
  const auto prepared =
      prepare(owner, before, candidate, gear, event.terrain_version);
  if (!prepared) return std::unexpected{prepared.error()};
  const auto& p = *prepared;
  if (event.version != kMoonTerrainImpactVersion ||
      event.collider_version != kMoonTerrainImpactColliderVersion ||
      event.craft != before.craft || event.system != p.start.frame.system ||
      event.body != owner.body || event.triangle.body != owner.body ||
      event.tick != p.end.tick || event.source_checksum != p.source_checksum ||
      event.candidate_checksum != p.candidate_checksum ||
      !std::isfinite(event.fraction) || event.fraction < 0 ||
      event.fraction > 1 || !finite(event.contact_point_craft_metres) ||
      !finite(event.outward_normal_moon_fixed) ||
      !finite(event.pre_response_velocity_moon_fixed) ||
      !std::isfinite(event.closing_speed_metres_per_second) ||
      event.closing_speed_metres_per_second <= 0 ||
      (event.region != MoonCraftImpactRegion::forebody &&
       event.region != MoonCraftImpactRegion::landing_structure) ||
      (event.region == MoonCraftImpactRegion::forebody &&
       event.support_index != 0) ||
      (gear && event.support_index >= p.count - 1))
    return std::unexpected("Invalid, stale or non-inward moon impact witness");
  const auto sampler = MoonTerrainSampler::create(owner, event.terrain_version);
  if (!sampler)
    return std::unexpected("Moon impact response source unavailable");
  const auto triangle = build_moon_contact_triangle(*sampler, event.triangle);
  if (!triangle)
    return std::unexpected("Moon impact response triangle unavailable");
  const auto& part = p.parts[gear ? event.support_index + 1 : 0];
  const auto hit = interval(p, part, *triangle, 0);
  const auto contact = contact_point(p, part, *triangle, event.fraction);
  if (!hit || (*hit)[0] != event.fraction || !contact ||
      witness(p, event.terrain_version, part, *triangle, event.fraction,
              *contact) != event)
    return std::unexpected(
        "Moon witness differs from actual finite proxy contact");
  auto fixed = p.end;
  fixed.position_metres =
      add(add(p.start.position_metres, mul(p.delta, event.fraction)),
          mul(event.outward_normal_moon_fixed,
              kMoonTerrainImpactResponseRetreatMetres));
  fixed.orientation = p.start.orientation;
  const double inward = dot(fixed.linear_velocity_metres_per_second,
                            event.outward_normal_moon_fixed);
  if (inward < 0)
    fixed.linear_velocity_metres_per_second =
        sub(fixed.linear_velocity_metres_per_second,
            mul(event.outward_normal_moon_fixed, inward));
  fixed.angular_velocity_radians_per_second = {};
  const RigidBodyWorldContext context{owner};
  const auto canonical = canonicalize_rigid_body_state(context, fixed);
  if (!canonical)
    return std::unexpected("Moon impact fixed response is invalid");
  const auto reframed = reframe_rigid_body(
      context, *canonical, {candidate.frame, candidate.tick}, owner);
  if (!reframed)
    return std::unexpected("Moon impact native response reframe failed");
  return *reframed;
}
} // namespace apsis_drift
