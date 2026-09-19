#pragma once

#include "apsis_drift/rigid_body.hpp"

#include <array>

namespace apsis_drift {

inline constexpr std::uint32_t kTouchdownEnvelopeVersion{1};

enum class TouchdownSurface : std::uint8_t {
  solid = 1,
  water = 2,
  unsupported = 3
};

// Caller-supplied physical observations; NOT a terrain sampler or collision
// proof. Gaps are signed metres along outward_normal from terrain to the
// uncompressed deployed pad footprint: positive means separated. Bounds cover
// the WHOLE footprint, not merely its centre. Each gap must be finite with
// magnitude <= kRigidBodyMaximumPositionMetres. Normal squared norm must be
// within kRigidBodyOrientationSquaredNormTolerance of 1; the assessor derives
// a unit normal without modifying this observation. It is a representative
// local planar-patch normal, NOT proof of every footprint point's slope.
// Outward requires strictly positive dot product with radial up at COM;
// inward and exactly tangential normals are malformed observations.
// footprint_supported attests geometric/bearing adequacy supplied by the
// caller: stroke bounds alone cannot establish support on a nonplanar patch.
// Unused slots must compare equal to a zero-initialized observation.
struct TouchdownPadObservation {
  std::uint8_t support_index{};
  double minimum_gap_metres{}, maximum_gap_metres{};
  RigidVector3 outward_normal;
  TouchdownSurface surface{};
  bool footprint_supported{};
  friend auto operator==(const TouchdownPadObservation&,
                         const TouchdownPadObservation&) -> bool = default;
};

struct TouchdownObservations {
  PlanetId planet;
  SimulationTick tick{};
  std::uint8_t support_count{};
  std::array<TouchdownPadObservation, 4> supports{};
  bool gear_deployed{};
};

enum class TouchdownMargin : std::uint32_t {
  gear_not_deployed = 1U << 0,
  not_upright = 1U << 1,
  unsupported_material = 1U << 2,
  unsupported_footprint = 1U << 3,
  separated_support = 1U << 4,
  compression_exceeded = 1U << 5,
  slope_exceeded = 1U << 6,
  separating_velocity = 1U << 7,
  descent_speed_exceeded = 1U << 8,
  tangential_speed_exceeded = 1U << 9,
  angular_speed_exceeded = 1U << 10,
  gravity_rating_exceeded = 1U << 11,
  pressure_rating_exceeded = 1U << 12,
  static_support_load_exceeded = 1U << 13,
};

enum class TouchdownClass : std::uint8_t {
  pads_clear,
  pad_contact_ready,
  pad_contact_unsafe
};

struct TouchdownPadAssessment {
  // Local failures only; global gear, radial upright, angular speed and
  // environment failures appear on TouchdownAssessment.
  std::uint32_t failed_margins{};
  // Authored contact-centre velocity v_COM + R(q)(omega_body cross offset).
  double normal_velocity_metres_per_second{}; // positive separates
  double tangential_speed_metres_per_second{};
  double compression_capacity_metres{}; // body-Y stroke projected onto normal
};

struct TouchdownAssessment {
  TouchdownClass classification{TouchdownClass::pads_clear};
  // Union of global and every support's failures, evaluated even pads_clear.
  std::uint32_t failed_margins{};
  std::uint8_t support_count{};
  std::array<TouchdownPadAssessment, 4> supports{};
};

enum class TouchdownError : std::uint8_t {
  invalid_state,
  unsupported_frame,
  invalid_craft_frame,
  observation_identity_mismatch,
  invalid_support_count,
  invalid_support_order,
  invalid_observation,
  invalid_normal,
  unsafe_arithmetic
};

// Pure pad-envelope assessment only. Requires canonical planet_fixed state and
// matching planet/tick observations. No height/normal sampling, hull clearance,
// continuous collision, landed transition, damage or floor-guard changes.
// Every authored upper bound is inclusive; separation >0 fails readiness.
// Upright requires body up . radial up >0 and body up . each normal >0.
// Radial up is evaluated at COM. Gravity/pressure are nominal descriptor
// surface ratings (not altitude-corrected). Each support must bear full static
// dry weight; no reaction-load distribution is solved. Version 1 accepts the
// registered 12000-millidegree slope rating using cosine 0.9781476007338057.
// Any minimum gap <=0 is a prospective deployed-pad contact candidate. It is
// ready only if every margin passes; otherwise unsafe. With every minimum
// gap >0 the result is pads_clear regardless of readiness failures. None of
// these classes establishes hull clearance or an actual landed/contact state.
[[nodiscard]] auto assess_touchdown_envelope(
    const RigidBodyWorldContext& context, const RigidBodyState& state,
    const TouchdownObservations& observations)
    -> std::expected<TouchdownAssessment, TouchdownError>;

} // namespace apsis_drift
