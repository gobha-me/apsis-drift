#pragma once

#include <cstdint>

namespace apsis_drift {
inline constexpr std::uint32_t kFreedomChartedArrivalVersion{1};
// Personal opt-in selection, not sensor evidence or an orbit-insertion aid.
struct FreedomChartedArrivalSelection {
  std::uint32_t version{kFreedomChartedArrivalVersion};
  friend auto operator==(const FreedomChartedArrivalSelection&,
                         const FreedomChartedArrivalSelection&)
      -> bool = default;
};
} // namespace apsis_drift
