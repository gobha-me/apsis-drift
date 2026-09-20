#pragma once

#include <expected>
#include <optional>

#include "apsis_drift/rigid_body.hpp"

namespace apsis_drift {

struct RigidFrameHandoffRequest {
  RigidCoordinateFrame destination;
  // Must equal the authoritative source tick; no fractional/render time.
  SimulationTick tick{};
};

enum class RigidFrameHandoffErrorCode : std::uint8_t {
  invalid_source,
  stale_tick,
  invalid_destination,
  unsupported_frame_pair,
  ephemeris_failure,
  invalid_result,
};

struct RigidFrameHandoffError {
  RigidFrameHandoffErrorCode code;
  std::optional<RigidBodyError> state_error;
  std::optional<LocalSystemError> ephemeris_error;
  friend auto operator==(const RigidFrameHandoffError&,
                         const RigidFrameHandoffError&) -> bool = default;
};

// Pure same-tick coordinate change, not a simulation step, dock/launch event or
// force provider. Supports system_inertial <-> station_relative_inertial using
// the authoritative origin station ephemeris. Their axes are identical: craft,
// tick, quaternion and body angular velocity retain every bit. A validated
// same-frame request is an exact identity, including planet_fixed. All other
// planet-fixed transitions are explicitly unsupported pending a canonical
// rotation recipe (#213). Failure never mutates source or context.
[[nodiscard]] auto reframe_rigid_body(const RigidBodyWorldContext& context,
                                      const RigidBodyState& source,
                                      const RigidFrameHandoffRequest& request)
    -> std::expected<RigidBodyState, RigidFrameHandoffError>;

} // namespace apsis_drift
