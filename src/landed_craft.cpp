#include "apsis_drift/landed_craft.hpp"

#include "apsis_drift/rigid_frame_handoff.hpp"
#include "apsis_drift/terrain_hull_clearance.hpp"
#include "apsis_drift/terrain_touchdown.hpp"

#include <cmath>
#include <limits>

namespace apsis_drift {
namespace {
auto support_and_hull(const PhysicalLocalSystem& owner,
                      const PhysicalPlanetRotationRecipe& rotation,
                      const RigidBodyState& source, bool gear)
    -> std::expected<RigidBodyState, LandedCraftError> {
  auto snapshot =
      TerrainTouchdownSnapshot::create(owner, rotation, source, gear);
  if (!snapshot) return std::unexpected{LandedCraftError::invalid_source};
  const auto query = snapshot->query();
  if (!query.assessment)
    return std::unexpected{LandedCraftError::contact_unknown};
  if (query.assessment->classification != TouchdownClass::pad_contact_ready)
    return std::unexpected{LandedCraftError::contact_not_ready};
  const auto hull = assess_terrain_hull_clearance(owner, rotation, source);
  if (!hull || !hull->certified)
    return std::unexpected{LandedCraftError::hull_not_certified};
  return query.geometry.provenance.fixed_query_state;
}
} // namespace

auto prepare_landed_craft(const PhysicalLocalSystem& owner,
                          const PhysicalPlanetRotationRecipe& rotation,
                          const RigidBodyState& source, bool gear_deployed,
                          std::uint64_t expected_source_checksum)
    -> std::expected<LandedCraftAnchor, LandedCraftError> {
  const auto checksum = rigid_body_state_checksum({owner}, source);
  if (!checksum) return std::unexpected{LandedCraftError::invalid_source};
  if (*checksum != expected_source_checksum)
    return std::unexpected{LandedCraftError::stale_source};
  if (!gear_deployed)
    return std::unexpected{LandedCraftError::gear_not_deployed};
  auto fixed = support_and_hull(owner, rotation, source, true);
  if (!fixed) return std::unexpected{fixed.error()};
  fixed->linear_velocity_metres_per_second = {};
  fixed->angular_velocity_radians_per_second = {};
  LandedCraftAnchor anchor{
      kLandedCraftAnchorVersion, kTerrainTouchdownPolicyVersion,
      rotation.rotation.version, rotation.owner_version, *fixed};
  if (auto valid = validate_landed_craft(owner, rotation, anchor, source.tick);
      !valid)
    return std::unexpected{valid.error()};
  return anchor;
}

auto resolve_landed_craft(const PhysicalLocalSystem& owner,
                          const PhysicalPlanetRotationRecipe& rotation,
                          const LandedCraftAnchor& anchor, SimulationTick tick)
    -> std::expected<RigidBodyState, LandedCraftError> {
  if (anchor.version != kLandedCraftAnchorVersion ||
      anchor.terrain_policy != kTerrainTouchdownPolicyVersion ||
      anchor.rotation_generator != rotation.rotation.version ||
      anchor.rotation_owner != rotation.owner_version)
    return std::unexpected{LandedCraftError::unsupported_version};
  const RigidBodyWorldContext context{owner};
  if (!validate_rigid_body_state(context, anchor.fixed) ||
      anchor.fixed.frame.kind != RigidFrameKind::planet_fixed ||
      anchor.fixed.frame.planet != rotation.rotation.planet ||
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
                         {{RigidFrameKind::planet_relative_inertial,
                           owner.catalog.id,
                           fixed.frame.planet,
                           {}},
                          tick},
                         rotation);
  if (!resolved)
    return std::unexpected{LandedCraftError::frame_resolution_failed};
  return *resolved;
}

auto validate_landed_craft(const PhysicalLocalSystem& owner,
                           const PhysicalPlanetRotationRecipe& rotation,
                           const LandedCraftAnchor& anchor, SimulationTick tick)
    -> std::expected<void, LandedCraftError> {
  const auto source = resolve_landed_craft(owner, rotation, anchor, tick);
  if (!source) return std::unexpected{source.error()};
  const auto geometry = support_and_hull(owner, rotation, *source, true);
  if (!geometry) return std::unexpected{geometry.error()};
  return {};
}

auto release_landed_craft(const PhysicalLocalSystem& owner,
                          const PhysicalPlanetRotationRecipe& rotation,
                          const LandedCraftAnchor& anchor, SimulationTick tick)
    -> std::expected<RigidBodyState, LandedCraftError> {
  if (auto valid = validate_landed_craft(owner, rotation, anchor, tick); !valid)
    return std::unexpected{valid.error()};
  const auto craft = resolve_craft_frame(anchor.fixed.craft);
  if (!craft || !supports_operation(craft->properties, CraftOperation::liftoff))
    return std::unexpected{LandedCraftError::invalid_anchor};
  const auto planet =
      find_local_system_planet(owner, *anchor.fixed.frame.planet);
  if (!planet) return std::unexpected{LandedCraftError::invalid_anchor};
  const auto gravity =
      static_cast<double>((*planet)->descriptor.surface_gravity.value) *
      9.80665 / 1000;
  const auto& p = craft->properties;
  // Body-Y starts away from its supported plane. Actual manoeuvre control must
  // retain this attitude and use rated thrust; this is no synthetic impulse.
  const auto& q = anchor.fixed.orientation;
  const auto norm2 = ((q.w * q.w + q.x * q.x) + q.y * q.y) + q.z * q.z;
  const RigidVector3 up{2 * (q.x * q.y - q.w * q.z) / norm2,
                        1 - 2 * (q.x * q.x + q.z * q.z) / norm2,
                        2 * (q.y * q.z + q.w * q.x) / norm2};
  const auto& position = anchor.fixed.position_metres;
  const auto radius = std::hypot(position.x, position.y, position.z);
  const auto vertical =
      ((up.x * position.x + up.y * position.y) + up.z * position.z) / radius;
  if (!std::isfinite(vertical) || vertical <= 0 ||
      static_cast<double>(p.positive_force_newtons[1]) * vertical <=
          p.dry_mass_kg * gravity)
    return std::unexpected{LandedCraftError::insufficient_liftoff_authority};
  return resolve_landed_craft(owner, rotation, anchor, tick);
}
} // namespace apsis_drift
