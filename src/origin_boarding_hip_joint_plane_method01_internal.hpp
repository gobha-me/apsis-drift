#pragma once
#include "apsis_drift/origin_boarding_hip_joint_plane_method01.hpp"
namespace apsis_drift::detail {
struct BoardingHipJointPlaneMethod01Limits {
  std::uint16_t capture_checks{8}, feature_checks{20}, chord_checks{190},
      operations{16384};
  std::size_t output_bytes{kBoardingHipJointPlaneMethod01MaximumOutputBytes};
};
[[nodiscard]] auto hip_joint_plane_method01_bounded(
    const OriginBoardingIntermediatePauseSupport&,
    BoardingHipJointPlaneMethod01Limits = {})
    -> BoardingHipJointPlaneMethod01Expected;
class BoardingHipJointPlaneMethod01FreshCallContext {
 public:
  BoardingHipJointPlaneMethod01FreshCallContext(
      const BoardingHipJointPlaneMethod01FreshCallContext&) = delete;
  BoardingHipJointPlaneMethod01FreshCallContext(
      BoardingHipJointPlaneMethod01FreshCallContext&&) = delete;
  auto operator=(const BoardingHipJointPlaneMethod01FreshCallContext&)
      -> BoardingHipJointPlaneMethod01FreshCallContext& = delete;
  auto operator=(BoardingHipJointPlaneMethod01FreshCallContext&&)
      -> BoardingHipJointPlaneMethod01FreshCallContext& = delete;
  [[nodiscard]] auto original() const
      -> const BoardingIntermediateEndpoint04Diagnostic& {
    return original_;
  }
  [[nodiscard]] auto owner() const
      -> const BoardingHipJointPlaneMethod01Diagnostic& {
    return owner_;
  }
  [[nodiscard]] auto provider() const
      -> const OriginBoardingIntermediatePauseSupport& {
    return provider_;
  }

 private:
  BoardingHipJointPlaneMethod01FreshCallContext(
      const BoardingIntermediateEndpoint04Diagnostic& o,
      BoardingHipJointPlaneMethod01Diagnostic& d,
      const OriginBoardingIntermediatePauseSupport& p)
      : original_(o), owner_(d), provider_(p) {}
  const BoardingIntermediateEndpoint04Diagnostic& original_;
  BoardingHipJointPlaneMethod01Diagnostic& owner_;
  const OriginBoardingIntermediatePauseSupport& provider_;
  friend auto hip_joint_plane_method01_bounded(
      const OriginBoardingIntermediatePauseSupport&,
      BoardingHipJointPlaneMethod01Limits)
      -> BoardingHipJointPlaneMethod01Expected;
};
} // namespace apsis_drift::detail
