#include "apsis_drift/planet_surface_walker.hpp"

#include "apsis_drift/terrain_touchdown.hpp"
#include "godot/contact_geometry_internal.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace apsis_drift {
namespace {
using V = RigidVector3;
auto add(V a, V b) -> V {
  return {a.x + b.x, a.y + b.y, a.z + b.z};
}
auto sub(V a, V b) -> V {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}
auto scale(V a, double s) -> V {
  return {a.x * s, a.y * s, a.z * s};
}
auto dot(V a, V b) -> double {
  return (a.x * b.x + a.y * b.y) + a.z * b.z;
}
auto length(V a) -> double {
  return std::hypot(a.x, a.y, a.z);
}
auto finite(V a) -> bool {
  return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z) &&
         std::max({std::abs(a.x), std::abs(a.y), std::abs(a.z)}) <= 1e8;
}
auto rotate(RigidOrientation q, V v) -> V {
  const double n = ((q.w * q.w + q.x * q.x) + q.y * q.y) + q.z * q.z;
  const V u{q.x, q.y, q.z};
  const V cross{u.y * v.z - u.z * v.y, u.z * v.x - u.x * v.z,
                u.x * v.y - u.y * v.x};
  return add(scale(v, (q.w * q.w - dot(u, u)) / n),
             add(scale(u, 2 * dot(u, v) / n), scale(cross, 2 * q.w / n)));
}
auto unit(V a) -> V {
  return scale(a, 1 / length(a));
}
auto basic_valid(const PlanetSurfaceWalkerState& s) -> bool {
  return s.version == kPlanetSurfaceWalkerVersion && s.actor_id == 1 &&
         finite(s.foot_position_metres) && length(s.foot_position_metres) > 1 &&
         finite(s.velocity_metres_per_second) &&
         length(s.velocity_metres_per_second) <= 2.45 &&
         std::isfinite(s.heading_radians) &&
         std::abs(s.heading_radians) <= std::numbers::pi;
}
} // namespace

auto PlanetSurfaceWalkTerrain::create(
    const PhysicalLocalSystem& owner,
    const PhysicalPlanetRotationRecipe& rotation,
    const LandedCraftAnchor& anchor, SimulationTick tick)
    -> std::expected<PlanetSurfaceWalkTerrain, std::string> {
  if (!validate_landed_craft(owner, rotation, anchor, tick))
    return std::unexpected{
        "Surface walking requires a valid supported landing"};
  if (anchor.fixed.craft !=
      CraftFrameRecipe{kWayfarerFrameId, kWayfarerFrameVersion})
    return std::unexpected{"Surface walking requires the registered Wayfarer"};
  const auto p = find_local_system_planet(owner, *anchor.fixed.frame.planet);
  if (!p) return std::unexpected{"Surface walking body owner is unavailable"};
  auto cache = TerrainTileCache::create(4);
  if (!cache) return std::unexpected{"Surface walking cache unavailable"};
  return PlanetSurfaceWalkTerrain{(*p)->descriptor, anchor, std::move(*cache)};
}

auto PlanetSurfaceWalkTerrain::ground(V direction)
    -> std::expected<V, PlanetSurfaceWalkStatus> {
  using namespace godot_spike;
  if (!finite(direction) || length(direction) <= 1)
    return std::unexpected{PlanetSurfaceWalkStatus::terrain_unresolved};
  const auto id = detail::locate_selected_contact_triangle(
      planet_, {direction.x, direction.y, direction.z});
  if (!id) return std::unexpected{PlanetSurfaceWalkStatus::terrain_unresolved};
  const auto triangle = detail::build_selected_contact_triangle(
      planet_, kExperimentalSavedContactSurface, *id, cache_);
  if (!triangle)
    return std::unexpected{PlanetSurfaceWalkStatus::terrain_unresolved};
  const V n{triangle->outward_normal.x, triangle->outward_normal.y,
            triangle->outward_normal.z};
  const auto& first = triangle->vertices[0];
  const V a{first.x, first.y, first.z};
  const auto up = unit(direction);
  const auto alignment = dot(n, up);
  // Fictional supported-walking envelope, independent of ship touchdown slope.
  if (!std::isfinite(alignment) || alignment < .8191520442889918)
    return std::unexpected{PlanetSurfaceWalkStatus::slope};
  const auto radius = static_cast<double>(planet_.radius.value) * 1000;
  const auto radial = unit(a);
  for (auto vertex : triangle->vertices)
    if (dot(radial, {vertex.x, vertex.y, vertex.z}) <= radius + .0001)
      return std::unexpected{PlanetSurfaceWalkStatus::water};
  const auto distance = dot(n, a) / alignment;
  if (!std::isfinite(distance) || distance <= radius || distance > 1e8)
    return std::unexpected{PlanetSurfaceWalkStatus::terrain_unresolved};
  return scale(up, distance);
}

auto PlanetSurfaceWalkTerrain::blocked(V from, V to) const -> bool {
  const auto craft = resolve_craft_frame(anchor_.fixed.craft);
  if (!craft) return true;
  const auto q = anchor_.fixed.orientation;
  const RigidOrientation inverse{q.w, -q.x, -q.y, -q.z};
  // Conservative swept standing capsule bounds in the craft's fixed box.
  // This does not purport to be mesh-level or articulated human collision.
  V low{1e8, 1e8, 1e8}, high{-1e8, -1e8, -1e8};
  for (auto point : {from, to}) {
    const auto up = unit(point);
    for (auto end : {point, add(point, scale(up, kOriginWalkerHeightMetres))}) {
      const auto p = rotate(inverse, sub(end, anchor_.fixed.position_metres));
      low = {std::min(low.x, p.x), std::min(low.y, p.y), std::min(low.z, p.z)};
      high = {std::max(high.x, p.x), std::max(high.y, p.y),
              std::max(high.z, p.z)};
    }
  }
  const auto& p = craft->properties;
  const auto r = kOriginWalkerHalfWidthMetres;
  return high.x + r >= p.hull_min_mm[0] / 1000.0 &&
         low.x - r <= p.hull_max_mm[0] / 1000.0 &&
         high.y + r >= p.hull_min_mm[1] / 1000.0 &&
         low.y - r <= p.hull_max_mm[1] / 1000.0 &&
         high.z + r >= p.hull_min_mm[2] / 1000.0 &&
         low.z - r <= p.hull_max_mm[2] / 1000.0;
}

auto PlanetSurfaceWalkTerrain::entry()
    -> std::expected<PlanetSurfaceWalkerState, std::string> {
  // Ground transfer ends behind the registered aft box, near the roof hatch.
  // Authored transfer presentation is separate from ordinary ground movement.
  const auto target = add(anchor_.fixed.position_metres,
                          rotate(anchor_.fixed.orientation, {0, 0, 8.5}));
  auto foot = ground(target);
  if (!foot || blocked(*foot, *foot))
    return std::unexpected{"Hatch ground access is unsupported or obstructed"};
  return PlanetSurfaceWalkerState{1, 1, anchor_.fixed.frame, *foot, {}, 0};
}

auto PlanetSurfaceWalkTerrain::validate(const PlanetSurfaceWalkerState& s)
    -> std::expected<void, std::string> {
  if (!basic_valid(s) || s.frame != anchor_.fixed.frame)
    return std::unexpected{"Invalid planet-fixed walker or body owner"};
  const auto supported = ground(s.foot_position_metres);
  if (!supported || length(sub(*supported, s.foot_position_metres)) > .0001 ||
      blocked(s.foot_position_metres, s.foot_position_metres))
    return std::unexpected{"Surface walker support or craft clearance refused"};
  return {};
}

auto PlanetSurfaceWalkTerrain::advance(const PlanetSurfaceWalkerState& s,
                                       const OriginWalkControls& controls,
                                       SimulationSeconds step)
    -> std::expected<PlanetSurfaceWalkStep, std::string> {
  if (!basic_valid(s) || !std::isfinite(step.count()) ||
      step != kSimulationStep || !std::isfinite(controls.forward) ||
      !std::isfinite(controls.right) ||
      !std::isfinite(controls.heading_radians) ||
      std::abs(controls.forward) > 1 || std::abs(controls.right) > 1 ||
      std::abs(controls.heading_radians) > std::numbers::pi)
    return std::unexpected{"Invalid surface walking state, controls or step"};
  if (auto valid = validate(s); !valid) return std::unexpected{valid.error()};
  auto next = s;
  next.heading_radians = controls.heading_radians;
  next.velocity_metres_per_second = {};
  const auto up = unit(s.foot_position_metres);
  const auto craft_right = rotate(anchor_.fixed.orientation, {1, 0, 0});
  const auto projected_right =
      sub(craft_right, scale(up, dot(craft_right, up)));
  if (length(projected_right) < 1e-8)
    return std::unexpected{"Surface walking heading frame is unavailable"};
  const auto reference_right = unit(projected_right);
  const V reference_back{reference_right.y * up.z - reference_right.z * up.y,
                         reference_right.z * up.x - reference_right.x * up.z,
                         reference_right.x * up.y - reference_right.y * up.x};
  const auto c = std::cos(controls.heading_radians),
             sn = std::sin(controls.heading_radians);
  const auto right = add(scale(reference_right, c), scale(reference_back, -sn));
  const auto forward =
      add(scale(reference_right, -sn), scale(reference_back, -c));
  auto demand =
      add(scale(forward, controls.forward), scale(right, controls.right));
  demand = scale(demand, kOriginWalkSpeedMetresPerSecond /
                             std::max(1.0, length(demand)));
  if (length(demand) == 0)
    return PlanetSurfaceWalkStep{next, PlanetSurfaceWalkStatus::supported};
  const auto proposal =
      add(s.foot_position_metres, scale(demand, step.count()));
  const auto foot = ground(proposal);
  if (!foot) return PlanetSurfaceWalkStep{next, foot.error()};
  const auto delta = sub(*foot, s.foot_position_metres);
  if (std::abs(dot(delta, up)) > .30)
    return PlanetSurfaceWalkStep{next, PlanetSurfaceWalkStatus::step};
  if (blocked(s.foot_position_metres, *foot))
    return PlanetSurfaceWalkStep{next,
                                 PlanetSurfaceWalkStatus::craft_obstruction};
  // Two metres/second is tangent demand; a 35-degree climb can have up to
  // 2/cos(35 degrees) metres/second of actual three-dimensional travel.
  if (length(delta) > 2.45 * step.count())
    return PlanetSurfaceWalkStep{next, PlanetSurfaceWalkStatus::slope};
  next.foot_position_metres = *foot;
  next.velocity_metres_per_second = scale(delta, 1 / step.count());
  return PlanetSurfaceWalkStep{next, PlanetSurfaceWalkStatus::supported};
}

auto PlanetSurfaceWalkTerrain::near_entry(const PlanetSurfaceWalkerState& s)
    -> std::expected<bool, std::string> {
  if (auto valid = validate(s); !valid) return std::unexpected{valid.error()};
  auto start = entry();
  if (!start) return std::unexpected{start.error()};
  return length(sub(start->foot_position_metres, s.foot_position_metres)) <=
         3.5;
}
} // namespace apsis_drift
