#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

#include "apsis_drift/rigid_body.hpp"

namespace apsis_drift {
inline constexpr std::uint32_t kOriginWalkGeometryVersion{1};
inline constexpr double kOriginWalkSpeedMetresPerSecond{2.0};
inline constexpr double kOriginWalkerHalfWidthMetres{.32};
inline constexpr double kOriginWalkerHeightMetres{1.93};
inline constexpr double kOriginWalkerEyeHeightMetres{1.70};
struct OriginWalkerState {
  std::uint32_t geometry_version{kOriginWalkGeometryVersion};
  std::uint64_t actor_id{1};
  RigidVector3 foot_position_metres;
  RigidVector3 velocity_metres_per_second;
  double heading_radians{};
  friend auto operator==(const OriginWalkerState&, const OriginWalkerState&)
      -> bool = default;
};
// Heading zero faces station -Z. Positive heading rotates about +Y.
struct OriginWalkControls {
  double forward{}, right{}, heading_radians{};
};
// Strict immutable package validation is also exposed for corrupt-buffer tests.
[[nodiscard]] auto validate_origin_walk_geometry_json(std::string_view)
    -> std::expected<void, std::string>;
[[nodiscard]] auto validate_origin_walker(const OriginWalkerState&)
    -> std::expected<void, std::string>;
// Ordinary supported-interior kinematics, not an AG acceleration/failure model.
// The owning voyage advances the shared simulation clock; there is no actor
// tick. Obstructed/unsupported travel stops at its current supported pose.
[[nodiscard]] auto advance_origin_walker(const OriginWalkerState&,
                                         const OriginWalkControls&,
                                         SimulationSeconds = kSimulationStep)
    -> std::expected<OriginWalkerState, std::string>;
} // namespace apsis_drift
