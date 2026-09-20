#include "apsis_drift/rigid_frame_handoff.hpp"

namespace apsis_drift {

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

} // namespace apsis_drift
