#include "apsis_drift/rigid_frame_handoff.hpp"

namespace apsis_drift {
namespace {
auto add(RigidVector3 a, RigidVector3 b) -> RigidVector3 {
  return {a.x + b.x, a.y + b.y, a.z + b.z};
}
auto subtract(RigidVector3 a, RigidVector3 b) -> RigidVector3 {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}
auto cross(RigidVector3 a, RigidVector3 b) -> RigidVector3 {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
auto conjugate(RigidOrientation q) -> RigidOrientation {
  return {q.w, -q.x, -q.y, -q.z};
}
auto multiply(RigidOrientation a, RigidOrientation b) -> RigidOrientation {
  return {((a.w * b.w - a.x * b.x) - a.y * b.y) - a.z * b.z,
          ((a.w * b.x + a.x * b.w) + a.y * b.z) - a.z * b.y,
          ((a.w * b.y - a.x * b.z) + a.y * b.w) + a.z * b.x,
          ((a.w * b.z + a.x * b.y) - a.y * b.x) + a.z * b.w};
}
auto rotate(RigidOrientation q, RigidVector3 v) -> RigidVector3 {
  const auto product = multiply(multiply(q, {0, v.x, v.y, v.z}), conjugate(q));
  const double norm2 = ((q.w * q.w + q.x * q.x) + q.y * q.y) + q.z * q.z;
  return {product.x / norm2, product.y / norm2, product.z / norm2};
}
} // namespace

auto reframe_rigid_body(const RigidBodyWorldContext& context,
                        const RigidBodyState& source,
                        const RigidFrameHandoffRequest& request)
    -> std::expected<RigidBodyState, RigidFrameHandoffError> {
  using Code = RigidFrameHandoffErrorCode;
  if (const auto valid = validate_rigid_body_state(context, source); !valid)
    return std::unexpected{
        RigidFrameHandoffError{Code::invalid_source, valid.error(), {}}};
  if (request.tick != source.tick)
    return std::unexpected{RigidFrameHandoffError{Code::stale_tick, {}, {}}};

  auto candidate = source;
  candidate.frame = request.destination;
  // Numerical limits are frame-independent; this validates destination
  // ownership before any ephemeris work, without canonicalizing source input.
  if (const auto valid = validate_rigid_body_state(context, candidate); !valid)
    return std::unexpected{
        RigidFrameHandoffError{Code::invalid_destination, valid.error(), {}}};
  if (source.frame == request.destination) return source;

  const bool to_system =
      source.frame.kind == RigidFrameKind::station_relative_inertial &&
      request.destination.kind == RigidFrameKind::system_inertial;
  const bool to_station =
      source.frame.kind == RigidFrameKind::system_inertial &&
      request.destination.kind == RigidFrameKind::station_relative_inertial;
  if (!to_system && !to_station)
    return std::unexpected{
        RigidFrameHandoffError{Code::unsupported_frame_pair, {}, {}}};

  // Successful station frame validation above proves this pointer exists and
  // matches the named canonical origin station. Use the final system-space
  // station p/v, not separately rounded host + host-relative components.
  const auto station = resolve_origin_station_ephemeris(
      context.system, *context.station, {source.tick, 0.0});
  if (!station)
    return std::unexpected{
        RigidFrameHandoffError{Code::ephemeris_failure, {}, station.error()}};
  const auto transform = [to_system](double value, double origin) {
    return to_system ? value + origin : value - origin;
  };
  candidate.position_metres = {
      transform(source.position_metres.x, station->position.x),
      transform(source.position_metres.y, station->position.y),
      transform(source.position_metres.z, station->position.z)};
  candidate.linear_velocity_metres_per_second = {
      transform(source.linear_velocity_metres_per_second.x,
                station->velocity.x),
      transform(source.linear_velocity_metres_per_second.y,
                station->velocity.y),
      transform(source.linear_velocity_metres_per_second.z,
                station->velocity.z)};
  // Only the completed candidate is canonicalized (signed zeros); source q is
  // already canonical and is never renormalized or reconstructed from heading.
  const auto result = canonicalize_rigid_body_state(context, candidate);
  if (!result)
    return std::unexpected{
        RigidFrameHandoffError{Code::invalid_result, result.error(), {}}};
  return *result;
}

auto reframe_rigid_body(const RigidBodyWorldContext& context,
                        const RigidBodyState& source,
                        const RigidFrameHandoffRequest& request,
                        const PlanetRotationRecipe& rotation_recipe)
    -> std::expected<RigidBodyState, RigidFrameHandoffError> {
  using Code = RigidFrameHandoffErrorCode;
  if (const auto valid = validate_rigid_body_state(context, source); !valid)
    return std::unexpected{
        RigidFrameHandoffError{Code::invalid_source, valid.error(), {}}};
  if (request.tick != source.tick)
    return std::unexpected{RigidFrameHandoffError{Code::stale_tick, {}, {}}};
  auto candidate = source;
  candidate.frame = request.destination;
  if (const auto valid = validate_rigid_body_state(context, candidate); !valid)
    return std::unexpected{
        RigidFrameHandoffError{Code::invalid_destination, valid.error(), {}}};

  const bool to_system =
      source.frame.kind == RigidFrameKind::planet_fixed &&
      request.destination.kind == RigidFrameKind::system_inertial;
  const bool to_fixed =
      source.frame.kind == RigidFrameKind::system_inertial &&
      request.destination.kind == RigidFrameKind::planet_fixed;
  const bool identity = source.frame.kind == RigidFrameKind::planet_fixed &&
                        source.frame == request.destination;
  if (!to_system && !to_fixed && !identity)
    return std::unexpected{
        RigidFrameHandoffError{Code::unsupported_frame_pair, {}, {}}};
  const auto planet =
      to_fixed ? request.destination.planet : source.frame.planet;
  if (planet != rotation_recipe.planet)
    return std::unexpected{
        RigidFrameHandoffError{Code::invalid_rotation_recipe,
                               {},
                               {},
                               PlanetRotationError::owner_mismatch}};
  const auto geometry =
      resolve_planet_rotation(context.system, rotation_recipe, source.tick);
  if (!geometry)
    return std::unexpected{RigidFrameHandoffError{
        Code::invalid_rotation_recipe, {}, {}, geometry.error()}};
  if (identity) return source;

  const auto rotation = geometry->fixed_to_system;
  const auto inverse = conjugate(rotation);
  const RigidVector3 origin{geometry->planet_position.x,
                            geometry->planet_position.y,
                            geometry->planet_position.z};
  const RigidVector3 velocity{geometry->planet_velocity.x,
                              geometry->planet_velocity.y,
                              geometry->planet_velocity.z};
  const RigidVector3 omega{geometry->angular_velocity_radians_per_second.x,
                           geometry->angular_velocity_radians_per_second.y,
                           geometry->angular_velocity_radians_per_second.z};
  const auto attitude = normalize_rigid_orientation(
      multiply(to_system ? rotation : inverse, source.orientation));
  if (!attitude)
    return std::unexpected{
        RigidFrameHandoffError{Code::invalid_result, attitude.error(), {}}};
  candidate.orientation = *attitude;
  if (to_system) {
    const auto radius = rotate(rotation, source.position_metres);
    candidate.position_metres = add(origin, radius);
    candidate.linear_velocity_metres_per_second =
        add(add(velocity,
                rotate(rotation, source.linear_velocity_metres_per_second)),
            cross(omega, radius));
    // Angular velocities are body-resolved on both sides. Only the owning
    // frame's spin is added; rotating the body's input components is wrong.
    const auto body_spin = rotate(conjugate(candidate.orientation), omega);
    candidate.angular_velocity_radians_per_second =
        add(source.angular_velocity_radians_per_second, body_spin);
  } else {
    const auto radius = subtract(source.position_metres, origin);
    candidate.position_metres = rotate(inverse, radius);
    candidate.linear_velocity_metres_per_second = rotate(
        inverse,
        subtract(subtract(source.linear_velocity_metres_per_second, velocity),
                 cross(omega, radius)));
    const auto body_spin = rotate(conjugate(source.orientation), omega);
    candidate.angular_velocity_radians_per_second =
        subtract(source.angular_velocity_radians_per_second, body_spin);
  }
  const auto result = canonicalize_rigid_body_state(context, candidate);
  if (!result)
    return std::unexpected{
        RigidFrameHandoffError{Code::invalid_result, result.error(), {}}};
  return *result;
}

} // namespace apsis_drift
