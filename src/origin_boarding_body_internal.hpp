#pragma once

#include "apsis_drift/origin_boarding_body.hpp"

// Purpose-specific private diagnostic seams, not gameplay or source authority.
// Numerical domain: coordinates bounded by 16 m, box half dimensions/capsule
// radii in [1e-6,4] m, conditioned right-handed frames. Degenerate capsules are
// spheres; nonzero axes whose squared lengths underflow refuse. Unsupported
// primitives return invalid_geometry; these are not source-clearance limits.
namespace apsis_drift::detail {
struct BoardingBodyNarrowIntersection {
  BoardingBodyIntersection intersection{};
  std::optional<BoardingBodyInteriorWitness> witness;
};
// Internal active R_y(yaw) R_x(pitch); inputs here alone are radians.
[[nodiscard]] auto boarding_body_rotation(double yaw_radians,
                                          double pitch_radians)
    -> BoardingBodyFrame;
[[nodiscard]] auto boarding_body_box_interior_depth(const BoardingBodyBox&,
                                                    RigidVector3) -> double;
[[nodiscard]] auto boarding_body_capsule_interior_depth(
    const BoardingBodyCapsule&, RigidVector3) -> double;
[[nodiscard]] auto boarding_body_box_box(const BoardingBodyBox&,
                                         const BoardingBodyBox&)
    -> BoardingBodyNarrowIntersection;
[[nodiscard]] auto boarding_body_capsule_box(const BoardingBodyCapsule&,
                                             const BoardingBodyBox&)
    -> BoardingBodyNarrowIntersection;
[[nodiscard]] auto boarding_body_capsule_capsule(const BoardingBodyCapsule&,
                                                 const BoardingBodyCapsule&)
    -> BoardingBodyNarrowIntersection;
} // namespace apsis_drift::detail
