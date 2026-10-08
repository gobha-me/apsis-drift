#pragma once
#include "origin_boarding_route_intermediate_root_z01_internal.hpp"
namespace apsis_drift::detail {
// Nonowning arithmetic ledger. This is never a report, issuer or capability.
// Construct only after validating the new owning provider and fresh token.
struct RootZ01SupportBorrow {
  const OriginBoardingIntermediatePauseSupport& source;
  std::array<BoardingRoutePhasePartBinding, kBoardingBodyPartCount>& parts;
  BoardingRouteIntermediateSupportSelf01Counters& work;
  std::array<bool, 67>& source_evaluated;
  std::array<bool, 4>& join_expression_identity;
  bool& arithmetic_supported;
  bool& source_enrolled;
  double& reporting_elapsed_seconds;
  double requested_first, requested_last;
  bool reverse;
  static constexpr std::uint32_t support_version{1}, self_version{1};
  BoardingRouteIntermediateSupportSelf01State state{};
  BoardingRouteIntermediateSupportSelf01Condition stop_condition{};
  explicit RootZ01SupportBorrow(BoardingRouteIntermediateRootZ01Diagnostic& d,
                                const OriginBoardingIntermediatePauseSupport& p)
      : source(p), parts(d.parts), work(d.work.support_self),
        source_evaluated(d.source_evaluated),
        join_expression_identity(d.join_expression_identity),
        arithmetic_supported(d.arithmetic_supported),
        source_enrolled(d.source_enrolled),
        reporting_elapsed_seconds(d.reporting_elapsed_seconds),
        requested_first(d.requested_first), requested_last(d.requested_last),
        reverse(d.reverse) {}
};
auto root_z01_definition_charge(
    RootZ01SupportBorrow&, BoardingRouteIntermediateSupportSelf01Cell&,
    const BoardingRouteIntermediateSupportSelf01Limits&,
    BoardingRouteIntermediateSupportSelf01Refusal&) -> bool;
auto root_z01_allocate(RootZ01SupportBorrow&,
                       BoardingRouteIntermediateSupportSelf01Cell&,
                       const BoardingRouteIntermediateSupportSelf01Limits&,
                       BoardingRouteIntermediateSupportSelf01Refusal&) -> bool;
auto root_z01_enroll_self_source(
    RootZ01SupportBorrow&, const BoardingRouteIntermediateSupportSelf01Limits&,
    BoardingRouteIntermediateSupportSelf01Refusal&) -> bool;
auto root_z01_prefix_math(const BoardingRouteFootPhaseRequest&,
                          RootZ01SupportBorrow&,
                          BoardingRouteIntermediateSupportSelf01Cell&,
                          const BoardingRouteIntermediateSupportSelf01Limits&,
                          BoardingRouteIntermediateSupportSelf01Refusal&)
    -> BoardingRouteIntermediateSupportSelf01State;
auto root_z01_load_math(const BoardingRouteFootPhaseRequest&,
                        RootZ01SupportBorrow&,
                        BoardingRouteIntermediateSupportSelf01Cell&,
                        const BoardingRouteIntermediateSupportSelf01Limits&,
                        BoardingRouteIntermediateSupportSelf01Refusal&)
    -> BoardingRouteIntermediateSupportSelf01State;
auto root_z01_self_math(const BoardingRouteFootPhaseRequest&,
                        RootZ01SupportBorrow&,
                        BoardingRouteIntermediateSupportSelf01Cell&,
                        const BoardingRouteIntermediateSupportSelf01Limits&,
                        BoardingRouteIntermediateSupportSelf01Refusal&)
    -> BoardingRouteIntermediateSupportSelf01State;
} // namespace apsis_drift::detail

#include "origin_boarding_route_checkpoint_world_material_internal.hpp"
namespace apsis_drift::detail {
auto root_z01_proxy_math(const BoardingRouteIntermediateRootZ01CurrentToken&,
                         BoardingRouteIntermediateRootZ01Diagnostic&,
                         BoardingRouteIntermediateRootZ01Cell&,
                         std::size_t part)
    -> std::expected<BoardingRouteCheckpointWorldProxy, std::string>;
using RootZ01ProxyGetter =
    std::expected<BoardingRouteCheckpointWorldProxy, std::string> (*)(
        void*, std::size_t cell, std::size_t part,
        BoardingRouteIntermediateRootZ01Refusal&);
auto root_z01_world_sweep(BoardingRouteIntermediateRootZ01Diagnostic&,
                          const BoardingRouteIntermediateRootZ01Limits&, void*,
                          RootZ01ProxyGetter,
                          BoardingRouteIntermediateRootZ01Refusal&)
    -> BoardingRouteIntermediateRootZ01State;
} // namespace apsis_drift::detail
