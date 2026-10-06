#include "apsis_drift/origin_boarding_route_checkpoint_unload_self02.hpp"
#include "origin_boarding_route_checkpoint_unload_self02_internal.hpp"
namespace apsis_drift {
auto assess_origin_boarding_route_checkpoint_unload_self02(
    const OriginBoardingBootSupport& source, double first, double last)
    -> std::expected<BoardingRouteCheckpointUnloadSelf02Diagnostic,
                     std::string> {
  return detail::boarding_route_checkpoint_unload_self02_bounded(source, first,
                                                                 last);
}
} // namespace apsis_drift
