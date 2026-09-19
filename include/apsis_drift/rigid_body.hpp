#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>

#include "apsis_drift/craft_frame.hpp"
#include "apsis_drift/local_system.hpp"

namespace apsis_drift {

inline constexpr std::uint32_t kRigidBodyStateVersion{1};
inline constexpr std::size_t kMaximumRigidBodyDocumentBytes{4096};
inline constexpr double kRigidBodyMaximumPositionMetres{1.0e15};
inline constexpr double kRigidBodyMaximumVelocityMetresPerSecond{1.0e9};
inline constexpr double kRigidBodyMaximumAngularVelocityRadiansPerSecond{100.0};
inline constexpr double kRigidBodyOrientationSquaredNormTolerance{1.0e-12};

struct RigidVector3 {
  double x{}, y{}, z{};
  friend auto operator==(const RigidVector3&, const RigidVector3&)
      -> bool = default;
};

// Right-handed unit quaternion, WXYZ order, active body-to-owning-frame
// rotation. Body axes follow CraftBodyAxes (+X right, +Y up, +Z back). q and -q
// are the only orientation aliases promised bit-identical canonicalization;
// approximate scalar multiples are not an equivalence class of floating-point
// states.
struct RigidOrientation {
  double w{1.0}, x{}, y{}, z{};
  friend auto operator==(const RigidOrientation&, const RigidOrientation&)
      -> bool = default;
};

enum class RigidFrameKind : std::uint8_t {
  system_inertial = 1,
  planet_fixed = 2,
  // Translated with the named station; axes remain system-inertial aligned.
  station_relative_inertial = 3,
};

struct RigidCoordinateFrame {
  RigidFrameKind kind{RigidFrameKind::system_inertial};
  SystemId system;
  std::optional<PlanetId> planet;
  std::optional<OriginStationId> station;
  friend auto operator==(const RigidCoordinateFrame&,
                         const RigidCoordinateFrame&) -> bool = default;
};

// Uses existing authoritative descriptors to resolve IDs. No alternate world
// catalog, frame transform or new stable body identity is introduced here.
struct RigidBodyWorldContext {
  const LocalSystemDescriptor& system;
  const OriginStationDescriptor* station{};
};

struct RigidBodyState {
  CraftFrameRecipe craft{kStarterShuttleFrameId, kStarterShuttleFrameVersion};
  RigidCoordinateFrame frame;
  SimulationTick tick{};
  RigidVector3 position_metres;
  RigidOrientation orientation;
  RigidVector3 linear_velocity_metres_per_second;
  // Relative to the owning frame, resolved along body axes; radians/second.
  RigidVector3 angular_velocity_radians_per_second;
  friend auto operator==(const RigidBodyState&, const RigidBodyState&)
      -> bool = default;
};

enum class RigidBodyError : std::uint8_t {
  invalid_world_context,
  unknown_system,
  unknown_planet,
  unknown_station,
  invalid_coordinate_frame,
  invalid_craft_frame,
  non_finite_state,
  excessive_magnitude,
  invalid_orientation,
  noncanonical_state,
  tick_overflow,
  document_too_large,
  malformed_json,
  duplicate_key,
  missing_field,
  unknown_field,
  invalid_type,
  invalid_decimal,
  unsupported_version,
};

// Explicit provider-side normalization only, never used during save hydration.
// Finite norm squared must be in [0.5,2]. Normalizes once using fixed WXYZ
// arithmetic order, then canonicalizes sign/zero and validates the result.
[[nodiscard]] auto normalize_rigid_orientation(RigidOrientation orientation)
    -> std::expected<RigidOrientation, RigidBodyError>;

// Canonicalizes q/-q and every signed zero, but does NOT normalize magnitude.
// Valid input orientations have |norm squared - 1| <= 1e-12. Valid state uses
// first nonzero WXYZ component positive; all zero doubles must be positive
// zero.
[[nodiscard]] auto canonicalize_rigid_body_state(
    const RigidBodyWorldContext& context, RigidBodyState candidate)
    -> std::expected<RigidBodyState, RigidBodyError>;
[[nodiscard]] auto validate_rigid_body_state(
    const RigidBodyWorldContext& context, const RigidBodyState& state)
    -> std::expected<void, RigidBodyError>;
[[nodiscard]] auto rigid_body_state_checksum(
    const RigidBodyWorldContext& context, const RigidBodyState& state)
    -> std::expected<std::uint64_t, RigidBodyError>;

// Separate native projection, NOT legacy save format 16 or a saved career.
// Strict bounded JSON; all doubles and 64-bit IDs/ticks are canonical decimal
// strings. Decode validates without normalization or mutation of live state.
[[nodiscard]] auto encode_rigid_body_state_json(
    const RigidBodyWorldContext& context, const RigidBodyState& state)
    -> std::expected<std::string, RigidBodyError>;
[[nodiscard]] auto decode_rigid_body_state_json(
    const RigidBodyWorldContext& context, std::string_view document)
    -> std::expected<RigidBodyState, RigidBodyError>;

} // namespace apsis_drift
