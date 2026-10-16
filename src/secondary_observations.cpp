#include "apsis_drift/secondary_observations.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace apsis_drift {
namespace {
auto difference(SystemPositionMetres a, SystemPositionMetres b)
    -> RigidVector3 {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}
auto magnitude(RigidVector3 v) -> double {
  return std::hypot(v.x, v.y, v.z);
}
auto unit(RigidVector3 v, double length) -> RigidVector3 {
  return {v.x / length, v.y / length, v.z / length};
}
auto blocked(RigidVector3 direction, double distance, SystemPositionMetres eye,
             SystemPositionMetres centre, double radius) -> bool {
  const auto c = difference(centre, eye);
  const double along =
      (c.x * direction.x + c.y * direction.y) + c.z * direction.z;
  if (along <= 0 || along >= distance) return false;
  // Cross product avoids cancellation between astronomical squared lengths.
  const RigidVector3 perpendicular{c.y * direction.z - c.z * direction.y,
                                   c.z * direction.x - c.x * direction.z,
                                   c.x * direction.y - c.y * direction.x};
  return magnitude(perpendicular) < radius;
}
} // namespace

auto observe_secondary_bodies(const SecondaryBodyCatalog& catalog,
                              SystemPositionMetres observer,
                              EphemerisQueryTime time)
    -> std::expected<std::array<SecondaryBodyObservation, 2>,
                     BodyEphemerisError> {
  if (!std::isfinite(observer.x) || !std::isfinite(observer.y) ||
      !std::isfinite(observer.z) ||
      std::max({std::abs(observer.x), std::abs(observer.y),
                std::abs(observer.z)}) > 1e15)
    return std::unexpected{BodyEphemerisError::unsafe_arithmetic};
  const auto hierarchy = make_body_hierarchy(catalog);
  if (!hierarchy) return std::unexpected{hierarchy.error()};
  const auto ephemerides = resolve_body_ephemerides(*hierarchy, time);
  if (!ephemerides) return std::unexpected{ephemerides.error()};
  const auto find = [&](BodyId id) -> const BodyEphemeris& {
    return *std::ranges::find(*ephemerides, id, &BodyEphemeris::body);
  };
  const auto& star = find(body_id(catalog.owner.catalog.star.id));
  std::array<SecondaryBodyObservation, 2> result;
  for (std::size_t i = 0; i < result.size(); ++i) {
    const auto& source = catalog.bodies[i];
    const auto& body = find(source.node.id);
    const auto delta = difference(body.position, observer);
    const double distance = magnitude(delta);
    const auto light = difference(star.position, body.position);
    const double light_distance = magnitude(light);
    if (!std::isfinite(distance) || !std::isfinite(light_distance) ||
        light_distance <= 0)
      return std::unexpected{BodyEphemerisError::unsafe_arithmetic};
    auto& out = result[i];
    out.id = source.node.id;
    out.tint = source.tint;
    out.distance_metres = distance;
    // At the centre there is no sightline; a fixed unit placeholder carries
    // only the explicit unavailable state and is never rendered.
    out.direction =
        distance > 0 ? unit(delta, distance) : RigidVector3{1, 0, 0};
    out.light_direction = unit(light, light_distance);
    const double phase =
        static_cast<double>(time.tick % source.spin_period_ticks) +
        time.sub_tick_fraction;
    const double turns =
        phase / source.spin_period_ticks +
        static_cast<double>(source.epoch_spin_phase_turns) / 4294967296.0;
    out.spin_radians =
        std::remainder(turns * 2 * std::numbers::pi, 2 * std::numbers::pi);
    if (distance <= static_cast<double>(source.radius_metres) / std::sin(0.2)) {
      out.status = SecondaryObservationStatus::close_unavailable;
      out.angular_radius = 0;
      continue;
    }
    out.angular_radius =
        std::asin(static_cast<double>(source.radius_metres) / distance);
    out.status = out.angular_radius >= 0.00002
                     ? SecondaryObservationStatus::visible
                     : SecondaryObservationStatus::unresolved;
    bool occulted = blocked(
        out.direction, distance, observer, star.position,
        static_cast<double>(catalog.owner.catalog.star.radius_kilometres) *
            1000);
    for (const auto& planet : catalog.owner.catalog.planets) {
      const auto& pose = find(body_id(planet.descriptor.id));
      occulted =
          occulted ||
          blocked(out.direction, distance, observer, pose.position,
                  static_cast<double>(planet.descriptor.radius.value) * 1000);
    }
    if (occulted) out.status = SecondaryObservationStatus::occluded;
  }
  std::ranges::sort(result, [](const auto& a, const auto& b) {
    if (a.distance_metres != b.distance_metres)
      return a.distance_metres > b.distance_metres;
    return a.id < b.id;
  });
  return result;
}
} // namespace apsis_drift
