#pragma once
// Bounded numeric pair seam for independent adversarial tests. The public
// world query admits only its selected provider and canonical body request.
#include "apsis_drift/origin_boarding_world_checkpoint.hpp"
#include <array>

namespace apsis_drift::detail {
struct BoardingWorldPairCertificate {
  bool certified{};
  std::size_t axes_examined{}, unsupported_axes{};
};
[[nodiscard]] auto boarding_world_checkpoint_pair(
    const BoardingSelfSolid&, const BoardingBootStanceTranslation&,
    const std::array<RigidVector3, 3>&) -> BoardingWorldPairCertificate;
} // namespace apsis_drift::detail
