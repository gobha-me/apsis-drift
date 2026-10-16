#pragma once

#include "apsis_drift/rigid_body.hpp"
#include "apsis_drift/secondary_bodies.hpp"

namespace apsis_drift {
enum class SecondaryObservationStatus : std::uint8_t {
  visible,
  unresolved,
  occluded,
  close_unavailable
};
struct SecondaryBodyObservation {
  BodyId id;
  SecondaryObservationStatus status{};
  RigidVector3 direction, light_direction;
  double angular_radius{}, spin_radians{}, distance_metres{};
  Rgb8 tint;
  friend auto operator==(const SecondaryBodyObservation&,
                         const SecondaryBodyObservation&) -> bool = default;
};
// Pure same-tick optical query. No gravity, surface, discovery or arrival
// target. Results are farthest first. Physical size is never enlarged for
// visibility.
[[nodiscard]] auto observe_secondary_bodies(const SecondaryBodyCatalog&,
                                            SystemPositionMetres observer,
                                            EphemerisQueryTime)
    -> std::expected<std::array<SecondaryBodyObservation, 2>,
                     BodyEphemerisError>;
} // namespace apsis_drift
