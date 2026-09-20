#pragma once

#include <expected>
#include <optional>

#include "apsis_drift/planet_rotation.hpp"
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
  invalid_rotation_recipe,
};

struct RigidFrameHandoffError {
  RigidFrameHandoffErrorCode code;
  std::optional<RigidBodyError> state_error;
  std::optional<LocalSystemError> ephemeris_error;
  std::optional<PlanetRotationError> rotation_error{};
  friend auto operator==(const RigidFrameHandoffError&,
                         const RigidFrameHandoffError&) -> bool = default;
};

// Pure same-tick coordinate change, not a simulation step, dock/launch event or
// force provider. Supports system_inertial <-> station_relative_inertial using
// the authoritative origin station ephemeris. Their axes are identical: craft,
// tick, quaternion and body angular velocity retain every bit. A validated
// same-frame request is an exact identity, including planet_fixed. All other
// planet-fixed transitions remain unsupported by this original overload.
// Failure never mutates source or context.
[[nodiscard]] auto reframe_rigid_body(const RigidBodyWorldContext& context,
                                      const RigidBodyState& source,
                                      const RigidFrameHandoffRequest& request)
    -> std::expected<RigidBodyState, RigidFrameHandoffError>;

// Explicit opt-in interpretation of the planet-fixed side using the supplied
// owner-qualified rotation recipe. Supports system_inertial <-> that planet's
// planet_fixed frame and a validated same-planet identity. All other pairs,
// including system identity/station pairs, refuse; no ignored-recipe fallback.
// Full p/v/q/body-resolved relative angular velocity changes at the same tick.
// Newly composed attitude is normalized once; source/hydration never are.
// Caller must retain the selected recipe outside standalone rigid-state JSON;
// this overload does not migrate or infer the meaning of an old saved frame.
[[nodiscard]] auto reframe_rigid_body(
    const RigidBodyWorldContext& context, const RigidBodyState& source,
    const RigidFrameHandoffRequest& request,
    const PlanetRotationRecipe& rotation_recipe)
    -> std::expected<RigidBodyState, RigidFrameHandoffError>;

} // namespace apsis_drift
