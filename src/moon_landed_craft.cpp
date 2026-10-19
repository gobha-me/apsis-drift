#include "apsis_drift/moon_landed_craft.hpp"

#include "apsis_drift/moon_body_dynamics.hpp"
#include "apsis_drift/rigid_frame_handoff.hpp"

#include <cmath>
#include <limits>

namespace apsis_drift {
namespace {
auto support_and_hull(const PhysicalMoonDescriptor& moon,
                      const RigidBodyState& source, bool gear,
                      std::uint32_t terrain_version)
    -> std::expected<RigidBodyState, LandedCraftError> {
  const auto snapshot =
      MoonContactSnapshot::create(moon, source, gear, terrain_version);
  if (!snapshot) return std::unexpected{LandedCraftError::invalid_source};
  const auto query = snapshot->query();
  if (!query.assessment)
    return std::unexpected{LandedCraftError::contact_unknown};
  if (query.assessment->classification != TouchdownClass::pad_contact_ready)
    return std::unexpected{LandedCraftError::contact_not_ready};
  const auto hull = snapshot->hull_clearance();
  if (!hull || !hull->certified)
    return std::unexpected{LandedCraftError::hull_not_certified};
  return snapshot->provenance().fixed_query_state;
}
} // namespace

auto prepare_moon_landed_craft(const PhysicalMoonDescriptor& moon,
                               const RigidBodyState& source, bool gear,
                               std::uint64_t expected_checksum,
                               std::uint32_t terrain_version)
    -> std::expected<MoonLandedCraftAnchor, LandedCraftError> {
  if (terrain_version != kMoonTerrainRecipeVersion)
    return std::unexpected{LandedCraftError::unsupported_version};
  const auto checksum =
      rigid_body_state_checksum(RigidBodyWorldContext{moon}, source);
  if (!checksum) return std::unexpected{LandedCraftError::invalid_source};
  if (*checksum != expected_checksum)
    return std::unexpected{LandedCraftError::stale_source};
  if (!gear) return std::unexpected{LandedCraftError::gear_not_deployed};
  auto fixed = support_and_hull(moon, source, true, terrain_version);
  if (!fixed) return std::unexpected{fixed.error()};
  fixed->linear_velocity_metres_per_second = {};
  fixed->angular_velocity_radians_per_second = {};
  MoonLandedCraftAnchor anchor{kMoonLandedCraftAnchorVersion, terrain_version,
                               kPhysicalMoonRotationVersion, *fixed};
  if (const auto valid = validate_moon_landed_craft(moon, anchor, source.tick);
      !valid)
    return std::unexpected{valid.error()};
  return anchor;
}
auto resolve_moon_landed_craft(const PhysicalMoonDescriptor& moon,
                               const MoonLandedCraftAnchor& anchor,
                               SimulationTick tick)
    -> std::expected<RigidBodyState, LandedCraftError> {
  if (anchor.version != kMoonLandedCraftAnchorVersion ||
      anchor.terrain_version != kMoonTerrainRecipeVersion ||
      anchor.rotation_version != kPhysicalMoonRotationVersion)
    return std::unexpected{LandedCraftError::unsupported_version};
  const RigidBodyWorldContext context{moon};
  if (!validate_rigid_body_state(context, anchor.fixed) ||
      anchor.fixed.frame.kind != RigidFrameKind::body_fixed ||
      anchor.fixed.frame.body != moon.body ||
      anchor.fixed.linear_velocity_metres_per_second != RigidVector3{} ||
      anchor.fixed.angular_velocity_radians_per_second != RigidVector3{})
    return std::unexpected{LandedCraftError::invalid_anchor};
  if (tick < anchor.fixed.tick ||
      tick >= std::numeric_limits<SimulationTick>::max() - 1)
    return std::unexpected{LandedCraftError::stale_tick};
  auto fixed = anchor.fixed;
  fixed.tick = tick;
  const auto resolved =
      reframe_rigid_body(context, fixed,
                         {{RigidFrameKind::body_relative_inertial,
                           context.system.id,
                           {},
                           {},
                           moon.body},
                          tick},
                         moon);
  if (!resolved)
    return std::unexpected{LandedCraftError::frame_resolution_failed};
  return *resolved;
}
auto validate_moon_landed_craft(const PhysicalMoonDescriptor& moon,
                                const MoonLandedCraftAnchor& anchor,
                                SimulationTick tick)
    -> std::expected<void, LandedCraftError> {
  const auto source = resolve_moon_landed_craft(moon, anchor, tick);
  if (!source) return std::unexpected{source.error()};
  const auto geometry =
      support_and_hull(moon, *source, true, anchor.terrain_version);
  if (!geometry) return std::unexpected{geometry.error()};
  return {};
}
auto release_moon_landed_craft(const PhysicalMoonDescriptor& moon,
                               const MoonLandedCraftAnchor& anchor,
                               SimulationTick tick)
    -> std::expected<RigidBodyState, LandedCraftError> {
  if (const auto valid = validate_moon_landed_craft(moon, anchor, tick); !valid)
    return std::unexpected{valid.error()};
  const auto craft = resolve_craft_frame(anchor.fixed.craft);
  if (!craft || !supports_operation(craft->properties, CraftOperation::liftoff))
    return std::unexpected{LandedCraftError::invalid_anchor};
  const auto resolved = resolve_moon_landed_craft(moon, anchor, tick);
  if (!resolved) return std::unexpected{resolved.error()};
  const auto force =
      evaluate_moon_body_gravity(RigidBodyWorldContext{moon}, *resolved);
  if (!force) return std::unexpected{LandedCraftError::invalid_anchor};
  // Negative dry relief can have gravity above the nominal reference-surface
  // rating. Liftoff must beat the actual force, not a fabricated nominal floor.
  const auto& q = anchor.fixed.orientation;
  const auto norm2 = ((q.w * q.w + q.x * q.x) + q.y * q.y) + q.z * q.z;
  const RigidVector3 up{2 * (q.x * q.y - q.w * q.z) / norm2,
                        1 - 2 * (q.x * q.x + q.z * q.z) / norm2,
                        2 * (q.y * q.z + q.w * q.x) / norm2};
  const auto& p = anchor.fixed.position_metres;
  const auto radius = std::hypot(p.x, p.y, p.z);
  const auto vertical = ((up.x * p.x + up.y * p.y) + up.z * p.z) / radius;
  const auto& acceleration = force->acceleration_metres_per_second_squared;
  const auto gravity =
      std::hypot(acceleration.x, acceleration.y, acceleration.z);
  if (!std::isfinite(vertical) || vertical <= 0 || !std::isfinite(gravity) ||
      static_cast<double>(craft->properties.positive_force_newtons[1]) *
              vertical <=
          craft->properties.dry_mass_kg * gravity)
    return std::unexpected{LandedCraftError::insufficient_liftoff_authority};
  return *resolved;
}
} // namespace apsis_drift
