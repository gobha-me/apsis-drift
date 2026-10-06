#pragma once

#include "apsis_drift/origin_boarding_route_intermediate_step02.hpp"
#include "origin_boarding_route_foot_phase_internal.hpp"

namespace apsis_drift::detail {
struct BoardingRouteIntermediateStep02Limits {
  BoardingRouteFootPhaseLimits phase;
};
// Returns a copy for arithmetic inspection only; no caller request can enter
// the named producer. Graph fixtures use the unchanged phase01 cell compiler.
[[nodiscard]] auto boarding_route_intermediate_step02_controls(
    std::size_t phase) -> std::optional<BoardingRouteFootPhaseRequest>;
[[nodiscard]] auto boarding_route_intermediate_step02_clock(double global)
    -> double;
[[nodiscard]] auto boarding_route_intermediate_step02_local(std::size_t phase,
                                                            double global)
    -> double;
// Refuses invalid/cross-join intervals. Point joins belong to the preceding
// phase; positive intervals starting at a join belong to the following phase.
[[nodiscard]] auto boarding_route_intermediate_step02_phase(double first,
                                                            double last)
    -> std::optional<std::size_t>;
// Arithmetic-only structural C2 identity; the fixed quintic and hump have zero
// first/second endpoint jets, even across different valid physical durations.
[[nodiscard]] auto boarding_route_intermediate_step02_join_math(
    const BoardingRouteFootPhaseRequest&, const BoardingRouteFootPhaseRequest&)
    -> bool;
[[nodiscard]] auto boarding_route_intermediate_step02_bounded(
    double first, double last, BoardingRouteIntermediateStep02Limits = {})
    -> std::expected<BoardingRouteIntermediateStep02Diagnostic, std::string>;
} // namespace apsis_drift::detail
