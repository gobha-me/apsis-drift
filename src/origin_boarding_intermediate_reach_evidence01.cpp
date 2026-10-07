#include "origin_boarding_intermediate_reach_evidence01_internal.hpp"
namespace apsis_drift {
auto assess_origin_boarding_intermediate_reach_evidence01(
    const OriginBoardingIntermediatePauseSupport& source,
    BoardingIntermediateEndpoint01Candidate candidate)
    -> BoardingIntermediateReachEvidence01Expected {
  return detail::intermediate_reach_evidence01_bounded(source, candidate);
}
} // namespace apsis_drift
