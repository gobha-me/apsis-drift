#include "apsis_drift/rigid_frame_handoff.hpp"

#include <type_traits>

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

  const auto relative_kind = [](RigidFrameKind kind) {
    return kind == RigidFrameKind::station_relative_inertial ||
           kind == RigidFrameKind::planet_relative_inertial;
  };
  const bool to_system =
      relative_kind(source.frame.kind) &&
      request.destination.kind == RigidFrameKind::system_inertial;
  const bool to_relative =
      source.frame.kind == RigidFrameKind::system_inertial &&
      relative_kind(request.destination.kind);
  if (!to_system && !to_relative)
    return std::unexpected{
        RigidFrameHandoffError{Code::unsupported_frame_pair, {}, {}}};

  // Use the final selected C++ ephemeris once. Axes are system-aligned;
  // translation never reconstructs or renormalizes attitude/body spin.
  struct Origin {
    SystemPositionMetres position;
    SystemVelocityMetresPerSecond velocity;
  };
  const auto& relative = to_system ? source.frame : request.destination;
  const auto origin = [&]() -> std::expected<Origin, RigidFrameHandoffError> {
    if (relative.kind == RigidFrameKind::station_relative_inertial) {
      // Frame validation proves the canonical station pointer exists.
      if (const auto* physical = context.physical_owner()) {
        const auto resolved = resolve_origin_station_ephemeris(
            *physical, *context.station, {source.tick, 0.0});
        if (!resolved)
          return std::unexpected{RigidFrameHandoffError{
              Code::ephemeris_failure, {}, {}, {}, resolved.error()}};
        return Origin{resolved->position, resolved->velocity};
      }
      const auto resolved = resolve_origin_station_ephemeris(
          context.system, *context.station, {source.tick, 0.0});
      if (!resolved)
        return std::unexpected{RigidFrameHandoffError{
            Code::ephemeris_failure, {}, resolved.error()}};
      return Origin{resolved->position, resolved->velocity};
    }
    const auto* physical = context.physical_owner();
    if (physical == nullptr)
      return std::unexpected{
          RigidFrameHandoffError{Code::ephemeris_failure,
                                 {},
                                 {},
                                 {},
                                 PhysicalLocalSystemError::invalid_context}};
    const auto resolved = resolve_planet_ephemeris(*physical, *relative.planet,
                                                   {source.tick, 0.0});
    if (!resolved)
      return std::unexpected{RigidFrameHandoffError{
          Code::ephemeris_failure, {}, {}, {}, resolved.error()}};
    return Origin{resolved->position, resolved->velocity};
  }();
  if (!origin) return std::unexpected{origin.error()};
  const auto transform = [to_system](double value, double origin) {
    return to_system ? value + origin : value - origin;
  };
  candidate.position_metres = {
      transform(source.position_metres.x, origin->position.x),
      transform(source.position_metres.y, origin->position.y),
      transform(source.position_metres.z, origin->position.z)};
  candidate.linear_velocity_metres_per_second = {
      transform(source.linear_velocity_metres_per_second.x, origin->velocity.x),
      transform(source.linear_velocity_metres_per_second.y, origin->velocity.y),
      transform(source.linear_velocity_metres_per_second.z,
                origin->velocity.z)};
  // Only the completed candidate is canonicalized (signed zeros); source q is
  // already canonical and is never renormalized or reconstructed from heading.
  const auto result = canonicalize_rigid_body_state(context, candidate);
  if (!result)
    return std::unexpected{
        RigidFrameHandoffError{Code::invalid_result, result.error(), {}}};
  return *result;
}

namespace {
template <typename Recipe>
auto reframe_rotating(const RigidBodyWorldContext& context,
                      const RigidBodyState& source,
                      const RigidFrameHandoffRequest& request,
                      const Recipe& rotation_recipe)
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

  const bool relative_pair =
      (source.frame.kind == RigidFrameKind::planet_fixed &&
       request.destination.kind == RigidFrameKind::planet_relative_inertial) ||
      (source.frame.kind == RigidFrameKind::planet_relative_inertial &&
       request.destination.kind == RigidFrameKind::planet_fixed);
  const bool same_planet = source.frame.planet == request.destination.planet;
  const bool to_system =
      source.frame.kind == RigidFrameKind::planet_fixed &&
      (request.destination.kind == RigidFrameKind::system_inertial ||
       (relative_pair && same_planet));
  const bool to_fixed =
      request.destination.kind == RigidFrameKind::planet_fixed &&
      (source.frame.kind == RigidFrameKind::system_inertial ||
       (relative_pair && same_planet));
  const bool identity = source.frame.kind == RigidFrameKind::planet_fixed &&
                        source.frame == request.destination;
  if (!to_system && !to_fixed && !identity)
    return std::unexpected{
        RigidFrameHandoffError{Code::unsupported_frame_pair, {}, {}}};
  const auto planet =
      to_fixed ? request.destination.planet : source.frame.planet;
  const auto recipe_planet = [&] {
    if constexpr (std::is_same_v<Recipe, PhysicalPlanetRotationRecipe>)
      return rotation_recipe.rotation.planet;
    else
      return rotation_recipe.planet;
  }();
  if (planet != recipe_planet)
    return std::unexpected{
        RigidFrameHandoffError{Code::invalid_rotation_recipe,
                               {},
                               {},
                               PlanetRotationError::owner_mismatch}};
  const auto geometry =
      [&]() -> std::expected<PlanetRotationGeometry, PlanetRotationError> {
    if constexpr (std::is_same_v<Recipe, PhysicalPlanetRotationRecipe>) {
      if (!context.physical_owner())
        return std::unexpected{PlanetRotationError::owner_mismatch};
      const auto resolved = resolve_planet_rotation(
          *context.physical_owner(), rotation_recipe, source.tick);
      if (!resolved) return std::unexpected{resolved.error()};
      return resolved->geometry;
    } else {
      if (context.physical_owner())
        return std::unexpected{PlanetRotationError::owner_mismatch};
      return resolve_planet_rotation(context.system, rotation_recipe,
                                     source.tick);
    }
  }();
  if (!geometry)
    return std::unexpected{RigidFrameHandoffError{
        Code::invalid_rotation_recipe, {}, {}, geometry.error()}};
  if (identity) return source;

  const auto rotation = geometry->fixed_to_system;
  const auto inverse = conjugate(rotation);
  const RigidVector3 origin = relative_pair
                                  ? RigidVector3{}
                                  : RigidVector3{geometry->planet_position.x,
                                                 geometry->planet_position.y,
                                                 geometry->planet_position.z};
  const RigidVector3 velocity = relative_pair
                                    ? RigidVector3{}
                                    : RigidVector3{geometry->planet_velocity.x,
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

} // namespace

auto reframe_rigid_body(const RigidBodyWorldContext& context,
                        const RigidBodyState& source,
                        const RigidFrameHandoffRequest& request,
                        const PlanetRotationRecipe& rotation_recipe)
    -> std::expected<RigidBodyState, RigidFrameHandoffError> {
  return reframe_rotating(context, source, request, rotation_recipe);
}

auto reframe_rigid_body(const RigidBodyWorldContext& context,
                        const RigidBodyState& source,
                        const RigidFrameHandoffRequest& request,
                        const PhysicalPlanetRotationRecipe& rotation_recipe)
    -> std::expected<RigidBodyState, RigidFrameHandoffError> {
  return reframe_rotating(context, source, request, rotation_recipe);
}

} // namespace apsis_drift
