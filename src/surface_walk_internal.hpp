#pragma once

#include "apsis_drift/planet_surface_walker.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

// Shared supported-surface kinematics for actual planet and moon owners. The
// two concrete adapters supply their own authenticated top-facet query; this
// kernel neither resolves a body nor grants terrain/landing support.
namespace apsis_drift::surface_walk_detail {
using V = RigidVector3;
inline auto add(V a, V b) -> V {
  return {a.x + b.x, a.y + b.y, a.z + b.z};
}
inline auto sub(V a, V b) -> V {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}
inline auto scale(V a, double s) -> V {
  return {a.x * s, a.y * s, a.z * s};
}
inline auto dot(V a, V b) -> double {
  return (a.x * b.x + a.y * b.y) + a.z * b.z;
}
inline auto length(V a) -> double {
  return std::hypot(a.x, a.y, a.z);
}
inline auto finite(V a) -> bool {
  return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z) &&
         std::max({std::abs(a.x), std::abs(a.y), std::abs(a.z)}) <= 1e8;
}
inline auto rotate(RigidOrientation q, V v) -> V {
  const double n = ((q.w * q.w + q.x * q.x) + q.y * q.y) + q.z * q.z;
  const V u{q.x, q.y, q.z};
  const V cross{u.y * v.z - u.z * v.y, u.z * v.x - u.x * v.z,
                u.x * v.y - u.y * v.x};
  return add(scale(v, (q.w * q.w - dot(u, u)) / n),
             add(scale(u, 2 * dot(u, v) / n), scale(cross, 2 * q.w / n)));
}
inline auto unit(V a) -> V {
  return scale(a, 1 / length(a));
}
inline auto basic_valid(const PlanetSurfaceWalkerState& s) -> bool {
  return s.version == kPlanetSurfaceWalkerVersion && s.actor_id == 1 &&
         finite(s.foot_position_metres) && length(s.foot_position_metres) > 1 &&
         finite(s.velocity_metres_per_second) &&
         length(s.velocity_metres_per_second) <= 2.45 &&
         std::isfinite(s.heading_radians) &&
         std::abs(s.heading_radians) <= std::numbers::pi;
}

inline auto blocked(const RigidBodyState& fixed, V from, V to) -> bool {
  const auto craft = resolve_craft_frame(fixed.craft);
  if (!craft) return true;
  const auto q = fixed.orientation;
  const RigidOrientation inverse{q.w, -q.x, -q.y, -q.z};
  // Conservative swept standing capsule bounds in the craft's fixed box.
  // This does not purport to be mesh-level or articulated human collision.
  V low{1e8, 1e8, 1e8}, high{-1e8, -1e8, -1e8};
  for (auto point : {from, to}) {
    const auto up = unit(point);
    for (auto end : {point, add(point, scale(up, kOriginWalkerHeightMetres))}) {
      const auto p = rotate(inverse, sub(end, fixed.position_metres));
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

template <typename Ground>
auto entry(const RigidBodyState& fixed, Ground ground)
    -> std::expected<PlanetSurfaceWalkerState, std::string> {
  // Ground transfer ends behind the registered aft box, near the roof hatch.
  // Authored transfer presentation is separate from ordinary ground movement.
  const auto target =
      add(fixed.position_metres, rotate(fixed.orientation, {0, 0, 8.5}));
  auto foot = ground(target);
  if (!foot || blocked(fixed, *foot, *foot))
    return std::unexpected{"Hatch ground access is unsupported or obstructed"};
  return PlanetSurfaceWalkerState{1, 1, fixed.frame, *foot, {}, 0};
}

template <typename Ground>
auto validate(const RigidBodyState& fixed, const PlanetSurfaceWalkerState& s,
              Ground ground, const char* owner_error)
    -> std::expected<void, std::string> {
  if (!basic_valid(s) || s.frame != fixed.frame)
    return std::unexpected{owner_error};
  const auto supported = ground(s.foot_position_metres);
  if (!supported || length(sub(*supported, s.foot_position_metres)) > .0001 ||
      blocked(fixed, s.foot_position_metres, s.foot_position_metres))
    return std::unexpected{"Surface walker support or craft clearance refused"};
  return {};
}

template <typename Ground>
auto advance(const RigidBodyState& fixed, const PlanetSurfaceWalkerState& s,
             const OriginWalkControls& controls, SimulationSeconds step,
             Ground ground, const char* owner_error)
    -> std::expected<PlanetSurfaceWalkStep, std::string> {
  if (!basic_valid(s) || !std::isfinite(step.count()) ||
      step != kSimulationStep || !std::isfinite(controls.forward) ||
      !std::isfinite(controls.right) ||
      !std::isfinite(controls.heading_radians) ||
      std::abs(controls.forward) > 1 || std::abs(controls.right) > 1 ||
      std::abs(controls.heading_radians) > std::numbers::pi)
    return std::unexpected{"Invalid surface walking state, controls or step"};
  if (auto valid = validate(fixed, s, ground, owner_error); !valid)
    return std::unexpected{valid.error()};
  auto next = s;
  next.heading_radians = controls.heading_radians;
  next.velocity_metres_per_second = {};
  const auto up = unit(s.foot_position_metres);
  const auto craft_right = rotate(fixed.orientation, {1, 0, 0});
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
  if (blocked(fixed, s.foot_position_metres, *foot))
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

template <typename Ground>
auto return_access(const RigidBodyState& fixed,
                   const PlanetSurfaceWalkerState& s, Ground ground,
                   const char* owner_error)
    -> std::expected<PlanetSurfaceReturnAccess, std::string> {
  if (auto valid = validate(fixed, s, ground, owner_error); !valid)
    return std::unexpected{valid.error()};
  const auto start = entry(fixed, ground);
  if (!start) return std::unexpected{start.error()};
  const double distance =
      length(sub(start->foot_position_metres, s.foot_position_metres));
  return PlanetSurfaceReturnAccess{start->foot_position_metres, distance,
                                   distance <= 3.5};
}
} // namespace apsis_drift::surface_walk_detail
