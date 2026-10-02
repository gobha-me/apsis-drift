#pragma once

#include "apsis_drift/origin_boarding_self_model02.hpp"
#include <span>

// Private source-free checkpoint seams only. None consumes caller-created
// geometry through the public evaluator or grants source/actor/route authority.
namespace apsis_drift::detail {
enum class BoardingSelfMembership : std::uint8_t {
  outside,
  boundary,
  interior,
  unresolved,
  invalid_geometry
};
enum class BoardingSelfSign : std::uint8_t {
  negative,
  zero,
  positive,
  unsupported
};
struct BoardingSelfSupportBounds {
  double lower{}, upper{};
  bool supported{};
};
[[nodiscard]] auto boarding_self_model02_membership(const BoardingSelfSolid&,
                                                    RigidVector3)
    -> BoardingSelfMembership;
[[nodiscard]] auto boarding_self_model02_region_membership(
    const BoardingSelfRegion&, RigidVector3) -> BoardingSelfMembership;
[[nodiscard]] auto boarding_self_model02_support(const BoardingSelfSolid&,
                                                 RigidVector3)
    -> BoardingSelfSupportBounds;
[[nodiscard]] auto boarding_self_model02_pair(
    const BoardingSelfPart&, const BoardingSelfPart&,
    const std::optional<BoardingSelfConnectedRegion>&)
    -> BoardingSelfPairDiagnostic;
[[nodiscard]] auto boarding_self_model02_prefix(RigidVector3 joint,
                                                RigidVector3 toward,
                                                double length, double radius)
    -> std::expected<BoardingBodyCapsule, BoardingBodyError>;
// Fixed 256-scalar input bound. Includes product residual-domain/capacity
// refusal in the implementation; not a public arbitrary-algebra service.
[[nodiscard]] auto boarding_self_model02_linear_sign(std::span<const double>)
    -> BoardingSelfSign;
} // namespace apsis_drift::detail
