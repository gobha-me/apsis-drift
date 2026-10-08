#include "origin_boarding_knee_compatibility_diagnostic01_internal.hpp"
#include "origin_boarding_route_foot_phase_internal.hpp"

namespace apsis_drift {
namespace {
using D = BoardingKneeCompatibilityDiagnostic01Diagnostic;
using E = BoardingKneeCompatibilityDiagnostic01Expected;
using R = BoardingKneeCompatibilityDiagnostic01Refusal;
using W = BoardingKneeCompatibilityDiagnostic01Stop;
using S = BoardingKneeCompatibilityDiagnostic01State;
using C = BoardingKneeCompatibilityDiagnostic01SourceCondition;
using L = detail::BoardingKneeCompatibilityDiagnostic01Limits;

static_assert(sizeof(E) <=
              kBoardingKneeCompatibilityDiagnostic01MaximumExpectedBytes);
static_assert(sizeof(R) <= 192);
static_assert(sizeof(BoardingKneeCompatibilityDiagnostic01PrefixEvidence) <=
              264);
static_assert(sizeof(BoardingKneeCompatibilityDiagnostic01OptimisticEvidence) <=
              368);
static_assert(sizeof(L) == 32);
static_assert(sizeof(OriginBoardingIntermediatePauseSupport) == 16);

// Complete registered source-owner reservations, including current/pending
// returns and the entire historical source enrollment. These integer forecasts
// do not admit emitted frames, library/error paths, or native world startup.
constexpr auto headers =
    2 * kBoardingKneeCompatibilityDiagnostic01MaximumExpectedBytes;
constexpr auto preflight_live =
    headers + 4096 + 4 * std::size_t{32} + 2048 + 64 + 152 + 88 + 512;
constexpr auto room_live = preflight_live + 1368 + 384;
constexpr auto source_live =
    33208 + headers + 4 * std::size_t{32} + 384 + 1368 + 64 + 152 + 88 + 2048;
constexpr auto source_prefix_live = source_live + 4096;
constexpr auto prefix_live = room_live + 4096;
constexpr auto optimistic_live = room_live + 2048;
constexpr auto oracle_live = room_live - 512 + 4096 + 1024;
constexpr auto metadata_live = room_live - 512 + 1024;
constexpr auto creator_live = 7000 + 152 + 88;
constexpr auto creator_error_live =
    1040 + 376 + 80 + 96 + 32 + 1024 + 152 + 88 + 4096 + 320;
constexpr auto preflight_error_live = preflight_live - 512 + 4096 + 320;
constexpr auto room_error_live = room_live - 512 + 4096 + 320;
constexpr auto source_error_live = source_live + 4096 + 320;
constexpr auto post_prefix_error_live = room_error_live;
constexpr auto teardown_error_live = room_error_live;
static_assert(creator_live == 7240 && creator_live <= 8192);
static_assert(creator_error_live == 7304 && creator_error_live <= 8192);
static_assert(preflight_live == 10928 && room_live == 12680);
static_assert(source_live == 41280 && source_prefix_live == 45376);
static_assert(prefix_live == 16776 && optimistic_live == 14728);
static_assert(oracle_live == 17288 && metadata_live == 13192);
static_assert(headers == 3840 &&
              headers <=
                  kBoardingKneeCompatibilityDiagnostic01MaximumOutputBytes);
static_assert(preflight_error_live == 14832 && room_error_live == 16584);
static_assert(source_error_live == 45696 && post_prefix_error_live == 16584 &&
              teardown_error_live == 16584);
static_assert(source_prefix_live <=
              kBoardingKneeCompatibilityDiagnostic01MaximumScratchBytes);
static_assert(source_error_live <=
              kBoardingKneeCompatibilityDiagnostic01MaximumScratchBytes);

auto valid_limits(const L& limits) -> bool {
  const L maximum;
  return limits.source_guards <= maximum.source_guards &&
         limits.construction_operations <= maximum.construction_operations &&
         limits.construction_guards <= maximum.construction_guards &&
         limits.output_bytes <= maximum.output_bytes;
}
} // namespace

auto detail::knee_compatibility_diagnostic01_refuse(D& d, R& r, W why) -> bool {
  if (!d.has_refusal) {
    d.first_refusal = r;
    d.has_refusal = true;
  }
  d.stop_condition = why;
  d.complete = false;
  switch (why) {
    case W::output_capacity:
    case W::source_capacity:
    case W::guard_capacity:
    case W::operation_capacity: d.state = S::capacity; break;
    case W::unsupported_arithmetic:
      d.state = S::unsupported;
      d.arithmetic_supported = false;
      d.prefix.arithmetic_supported = false;
      break;
    case W::invalid_binding:
    case W::source_identity: d.state = S::identity; break;
    case W::prefix_domain: d.state = S::prefix_refused; break;
    case W::suffix_domain: d.state = S::suffix_domain_unavailable; break;
    case W::source_refusal: d.state = S::source_refused; break;
    case W::none:
    case W::invalid_limits: d.state = S::not_run; break;
  }
  return false;
}

auto detail::knee_compatibility_diagnostic01_charge(D& d, R& r,
                                                    std::uint64_t& work,
                                                    std::size_t limit, W why)
    -> bool {
  if (work >= limit) {
    r.condition = r.predicate_condition =
        why == W::source_capacity      ? C::source_capacity
        : why == W::guard_capacity     ? C::slice_guard_capacity
        : why == W::operation_capacity ? C::slice_operation_capacity
                                       : C::output_capacity;
    return knee_compatibility_diagnostic01_refuse(d, r, why);
  }
  ++work;
  return true;
}

auto detail::knee_compatibility_diagnostic01_bounded(
    const OriginBoardingIntermediatePauseSupport& provider, L limits) -> E {
  // Priority is FP, raised limits, ONE header room, then structured TWO-header
  // room. No source operand is read before the structured output check.
  if (!boarding_route_foot_phase_environment())
    return std::unexpected("compatibility01 unsupported floating point");
  if (!valid_limits(limits))
    return std::unexpected("compatibility01 invalid limits");
  if (limits.output_bytes < sizeof(E))
    return std::unexpected("compatibility01 output headers");
  E result(std::in_place, provider);
  auto& d = *result;
  d.prefix.preflight_guards = 1;
  d.output_capacity_bytes =
      boarding_knee_compatibility_diagnostic01_required_output_bytes();
  BoardingRouteFootPhaseRequest request;
  R reason;
  if (limits.output_bytes < d.output_capacity_bytes) {
    reason.condition = reason.predicate_condition = C::output_capacity;
    knee_compatibility_diagnostic01_refuse(d, reason, W::output_capacity);
    return result;
  }
  if (!knee_compatibility_diagnostic01_source_enroll(d, request, limits,
                                                     reason))
    return result;
  if (!knee_compatibility_diagnostic01_prefix(d, request, limits, reason))
    return result;
  knee_compatibility_diagnostic01_optimistic(d, limits, reason);
  return result;
}

auto assess_origin_boarding_knee_compatibility_diagnostic01(
    const OriginBoardingIntermediatePauseSupport& provider) -> E {
  return detail::knee_compatibility_diagnostic01_bounded(provider);
}
} // namespace apsis_drift
