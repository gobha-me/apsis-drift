#include "apsis_drift/physical_moon_rotation.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace apsis_drift {
namespace {
constexpr double tau{2.0 * std::numbers::pi};
auto zero(double value) -> double {
  return value == 0.0 ? 0.0 : value;
}
auto canonical(RigidVector3 v) -> RigidVector3 {
  return {zero(v.x), zero(v.y), zero(v.z)};
}
auto rotate(RigidOrientation q, RigidVector3 v) -> RigidVector3 {
  // Recipe1 has a system +Z pole. Divide by norm squared, since normalized
  // quaternion components still retain binary64 rounding residuals.
  const double n = q.w * q.w + q.z * q.z;
  const double c = (q.w * q.w - q.z * q.z) / n;
  const double s = (2.0 * q.w * q.z) / n;
  return canonical({c * v.x - s * v.y, s * v.x + c * v.y, v.z});
}
auto unit(RigidVector3 v, double length) -> RigidVector3 {
  return canonical({v.x / length, v.y / length, v.z / length});
}
} // namespace

auto resolve_physical_moon_rotation(const PhysicalMoonDescriptor& owner,
                                    SimulationTick tick, RigidVector3 observer)
    -> std::expected<PhysicalMoonRotationGeometry, PhysicalMoonRotationError> {
  using Error = PhysicalMoonRotationError;
  if (owner.version != kPhysicalMoonRecipeVersion)
    return std::unexpected{Error::unsupported_version};
  if (!validate_physical_moon_descriptor(owner))
    return std::unexpected{Error::invalid_owner};
  if (tick == std::numeric_limits<SimulationTick>::max())
    return std::unexpected{Error::invalid_tick};
  for (const auto value : {observer.x, observer.y, observer.z})
    if (!std::isfinite(value) ||
        std::abs(value) > kPhysicalMoonRotationMaximumObserverComponentMetres)
      return std::unexpected{Error::invalid_observer};
  const auto hierarchy = make_body_hierarchy(owner.catalog);
  if (!hierarchy) return std::unexpected{Error::invalid_owner};
  const auto ephemerides = resolve_body_ephemerides(*hierarchy, {tick, 0.0});
  if (!ephemerides) return std::unexpected{Error::unsafe_arithmetic};
  const auto moon =
      std::ranges::find(*ephemerides, owner.body, &BodyEphemeris::body);
  const auto star_id = owner.catalog.owner.catalog.star.id;
  const auto star =
      std::ranges::find(*ephemerides, body_id(star_id), &BodyEphemeris::body);
  if (moon == ephemerides->end() || star == ephemerides->end())
    return std::unexpected{Error::invalid_owner};
  const auto cycle = tick % owner.spin_period_ticks;
  // Modulo before conversion retains the exact integer clock at huge ticks;
  // the epoch word remains a 32-bit full turn, never rounded into tick units.
  const double turns =
      static_cast<double>(cycle) / owner.spin_period_ticks +
      static_cast<double>(owner.epoch_spin_phase_turns) / 4294967296.0;
  const double phase = zero(std::remainder(turns * tau, tau));
  const auto q = normalize_rigid_orientation(
      {std::cos(phase * .5), 0, 0, std::sin(phase * .5)});
  if (!q) return std::unexpected{Error::unsafe_arithmetic};
  const double spin =
      tau * static_cast<double>(kSimulationHz) / owner.spin_period_ticks;
  const auto star_fixed = rotate({q->w, -q->x, -q->y, -q->z},
                                 {star->position.x - moon->position.x,
                                  star->position.y - moon->position.y,
                                  star->position.z - moon->position.z});
  const RigidVector3 to_star{star_fixed.x - observer.x,
                             star_fixed.y - observer.y,
                             star_fixed.z - observer.z};
  const double distance = std::hypot(to_star.x, to_star.y, to_star.z);
  if (!std::isfinite(distance))
    return std::unexpected{Error::unsafe_arithmetic};
  if (distance <= 1e-6) return std::unexpected{Error::observer_at_star};
  const auto light_fixed = unit(to_star, distance);
  const auto light_system = rotate(*q, light_fixed);
  const double system_length =
      std::hypot(light_system.x, light_system.y, light_system.z);
  if (!std::isfinite(system_length) || system_length <= 0.0 ||
      !std::isfinite(spin))
    return std::unexpected{Error::unsafe_arithmetic};
  const auto normalized_system = unit(light_system, system_length);
  return PhysicalMoonRotationGeometry{
      kPhysicalMoonRotationVersion,
      owner,
      tick,
      cycle,
      phase,
      *q,
      {0, 0, spin},
      moon->position,
      moon->velocity,
      star_id,
      star_fixed,
      canonical(observer),
      light_fixed,
      {normalized_system.x, normalized_system.y, normalized_system.z},
      distance};
}
} // namespace apsis_drift
