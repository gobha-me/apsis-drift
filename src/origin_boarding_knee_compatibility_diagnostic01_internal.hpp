#pragma once
#include "apsis_drift/origin_boarding_knee_compatibility_diagnostic01.hpp"
#include "origin_boarding_intermediate_pause_support_internal.hpp"

namespace apsis_drift::detail {
struct BoardingKneeCompatibilityDiagnostic01Limits {
  std::size_t source_guards{64}, construction_operations{255},
      construction_guards{50}, output_bytes{16777216};
};
[[nodiscard]] auto knee_compatibility_diagnostic01_bounded(
    const OriginBoardingIntermediatePauseSupport&,
    BoardingKneeCompatibilityDiagnostic01Limits = {})
    -> BoardingKneeCompatibilityDiagnostic01Expected;
auto knee_compatibility_diagnostic01_refuse(
    BoardingKneeCompatibilityDiagnostic01Diagnostic&,
    BoardingKneeCompatibilityDiagnostic01Refusal&,
    BoardingKneeCompatibilityDiagnostic01Stop) -> bool;
auto knee_compatibility_diagnostic01_charge(
    BoardingKneeCompatibilityDiagnostic01Diagnostic&,
    BoardingKneeCompatibilityDiagnostic01Refusal&, std::uint64_t&, std::size_t,
    BoardingKneeCompatibilityDiagnostic01Stop) -> bool;
[[gnu::noinline]] auto knee_compatibility_diagnostic01_source_enroll(
    BoardingKneeCompatibilityDiagnostic01Diagnostic&,
    BoardingRouteFootPhaseRequest&,
    const BoardingKneeCompatibilityDiagnostic01Limits&,
    BoardingKneeCompatibilityDiagnostic01Refusal&) -> bool;
[[gnu::noinline]] auto knee_compatibility_diagnostic01_prefix(
    BoardingKneeCompatibilityDiagnostic01Diagnostic&,
    BoardingRouteFootPhaseRequest&,
    const BoardingKneeCompatibilityDiagnostic01Limits&,
    BoardingKneeCompatibilityDiagnostic01Refusal&) -> bool;
[[gnu::noinline]] auto knee_compatibility_diagnostic01_optimistic(
    BoardingKneeCompatibilityDiagnostic01Diagnostic&,
    const BoardingKneeCompatibilityDiagnostic01Limits&,
    BoardingKneeCompatibilityDiagnostic01Refusal&) -> bool;
} // namespace apsis_drift::detail
