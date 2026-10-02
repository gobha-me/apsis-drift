#pragma once

#include "apsis_drift/origin_boarding_self_model03.hpp"
#include "origin_boarding_self_model02_internal.hpp"

// Private source-free test seams. Custom fixture geometry supplies no
// public evaluator, actor, source-contact, support, save or route authority.
namespace apsis_drift::detail {
enum class BoardingSelfProofStatus03 : std::uint8_t {
  proved,
  family_inapplicable,
  arithmetic_unresolved,
  invalid_geometry
};
struct BoardingSelfProof03 {
  BoardingSelfProofStatus03 status{
      BoardingSelfProofStatus03::family_inapplicable};
  BoardingSelfCertificate03 certificate{BoardingSelfCertificate03::none};
};
struct BoardingSelfSlabSupport03 {
  // Outward enclosure of the UNNORMALIZED complete-box gap:
  // L*|d| - d.(C-A) - sum_j h_j*abs(d.F_j), in square metres.
  // The exact specialization may prove a zero/positive sign even when
  // these valid interval bounds straddle zero. No clearance is inferred.
  double gap_lower_square_metres{}, gap_upper_square_metres{};
  bool interval_supported{}, exact_specialization_used{};
  BoardingSelfProof03 proof;
};

[[nodiscard]] auto boarding_self_model03_region_membership(
    const BoardingSelfRegion03&, RigidVector3) -> BoardingSelfMembership;
[[nodiscard]] auto boarding_self_model03_slab_box_support(
    const BoardingSelfAxialSlab03&, const BoardingBodyBox&)
    -> BoardingSelfSlabSupport03;
// Represented N=c0 cross c1 from actual stored trunk columns, +N then -N.
// N is an arbitrary stored certificate direction, not an exact inverse row.
// Uses negative v upper<0, |N cross d|/|d| perpendicular support, and
// complete cylinder/distal maximum-support bounds below trunk minimum.
[[nodiscard]] auto boarding_self_model03_shoulder(const BoardingBodyCapsule&,
                                                  const BoardingSelfEllipsoid&,
                                                  const BoardingSelfSphere&)
    -> BoardingSelfProof03;
// Also checks canonical IDs/junction and exact binding/orientation of the
// slab's original capsule. Generic membership alone has no such pair ID.
[[nodiscard]] auto boarding_self_model03_pair(
    const BoardingSelfPart&, const BoardingSelfPart&,
    const std::optional<BoardingSelfConnectedRegion03>&)
    -> BoardingSelfPairDiagnostic03;

// Returns a positive norm only for the preregistered exact-norm family:
// difference residuals zero; guarded squares residuals zero; sequential
// square-sum residuals zero; guarded candidate sqrt squared equals D with
// zero residual. Unsupported/nonfinite/degenerate arithmetic returns null.
[[nodiscard]] auto boarding_self_model03_exact_norm(RigidVector3 root,
                                                    RigidVector3 toward)
    -> std::optional<double>;
// Sign of L*|d| - d.(p-A): positive is strictly inside the plane, zero is
// its exact boundary. Unsupported interval/exact arithmetic stays unsupported.
// Uses checked256 terms, with conservative raw bounds point26 / box98;
// every nonzero product requires ilogb(a)+ilogb(b)>=-970 before FMA.
[[nodiscard]] auto boarding_self_model03_axial_plane_sign(
    const BoardingSelfAxialSlab03&, RigidVector3) -> BoardingSelfSign;
} // namespace apsis_drift::detail
