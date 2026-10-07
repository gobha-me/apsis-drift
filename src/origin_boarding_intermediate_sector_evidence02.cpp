#include "apsis_drift/origin_boarding_intermediate_sector_evidence02.hpp"
#include "origin_boarding_intermediate_sector_evidence02_internal.hpp"
#include "origin_boarding_route_foot_phase_internal.hpp"
#include <cmath>
#include <limits>
#include <new>

namespace apsis_drift {
namespace {
using D = BoardingIntermediateSectorEvidence02Diagnostic;
using E = BoardingIntermediateSectorEvidence02Expected;
using R = BoardingIntermediateSectorEvidence02Refusal;
using W = BoardingIntermediateSectorEvidence02Condition;
using S = BoardingIntermediateSectorEvidence02State;
using Stage = BoardingIntermediateSectorEvidence02Stage;
using Original = BoardingIntermediateSectorEvidence02OriginalResult;
using Kind = BoardingIntermediateSectorEvidence02Kind;
using Sector = BoardingIntermediateSectorEvidence02Sector;
using Classification = BoardingIntermediateSectorEvidence02Classification;
using L = detail::BoardingIntermediateSectorEvidence02Limits;
using Context = detail::BoardingIntermediateSectorEvidence02AdmissionContext;
using Token = detail::BoardingIntermediateSectorEvidence02CaptureToken;
using Fresh = detail::BoardingIntermediateSectorEvidence02FreshCallRecord;
using Request = BoardingRouteFootPhaseRequest;
using Cell = BoardingRouteFootPhaseCell;
using A = detail::BoardingIntermediatePauseSupportAccess;
using OldW = BoardingIntermediateEndpoint03Condition;
using OldStage = BoardingIntermediateEndpoint03SelfStage;
using PhaseW = BoardingRouteFootPhaseCondition;
constexpr std::size_t fixed_output = 2 * sizeof(E);
constexpr std::uint32_t all_guards = (std::uint32_t{1} << 21) - 1;
constexpr auto all_source_operations =
    std::numeric_limits<std::uint64_t>::max();
constexpr std::array<std::uint64_t, 2> all_operations{all_source_operations,
                                                      0x3fffULL};
static_assert(sizeof(E) <=
              kBoardingIntermediateSectorEvidence02MaximumExpectedBytes);
static_assert(sizeof(L) <= 40);
static_assert(sizeof(R) <= 128);
static_assert(sizeof(Context) <= 40);
static_assert(sizeof(Token) <= 40);
static_assert(sizeof(Fresh) <= 16);
static_assert(sizeof(std::optional<Token>) <= 48);

auto stop(D& d, R& r, W why) -> bool {
  r.condition = why;
  if (!d.first_companion_refusal_available) {
    d.first_companion_refusal = r;
    d.first_companion_refusal_available = true;
  }
  d.terminal_companion_refusal = r;
  d.terminal_companion_refusal_available = true;
  d.last_stage = r.stage;
  d.operation = r.operation;
  if (d.original_result == Original::unsupported) {
    d.state = S::unsupported;
  } else if (d.original_result == Original::capacity) {
    d.state = S::capacity;
  } else if (why == W::unsupported_arithmetic) {
    d.state = S::unsupported;
  } else if (why == W::source_capacity ||
             why == W::construction_guard_capacity ||
             why == W::construction_operation_capacity ||
             why == W::capture_capacity || why == W::output_capacity) {
    if (d.state != S::unsupported) d.state = S::capacity;
  } else if (d.state != S::capacity && d.state != S::unsupported) {
    d.state = S::unresolved;
  }
  if (why == W::unsupported_arithmetic ||
      d.original_result == Original::unsupported) {
    d.arithmetic_supported = false;
    if (!d.slice.complete) d.slice.arithmetic_supported = false;
  }
  return false;
}

auto valid_limits(const L& l) -> bool {
  const L maximum;
  return l.source_guards <= maximum.source_guards &&
         l.construction_guards <= maximum.construction_guards &&
         l.construction_operations <= maximum.construction_operations &&
         l.capture_guards <= maximum.capture_guards &&
         l.output_bytes <= maximum.output_bytes;
}

auto mapped_condition(OldW why) -> W {
  switch (why) {
    case OldW::none: return W::none;
    case OldW::invalid_binding: return W::invalid_binding;
    case OldW::source_capacity: return W::source_capacity;
    case OldW::source_identity: return W::source_identity;
    case OldW::slice_guard_capacity: return W::construction_guard_capacity;
    case OldW::slice_operation_capacity:
      return W::construction_operation_capacity;
    case OldW::unsupported_arithmetic: return W::unsupported_arithmetic;
    case OldW::slice_unavailable: return W::slice_unavailable;
    default: return W::slice_identity;
  }
}

auto mapped_stage(OldStage stage) -> Stage {
  if (stage == OldStage::source) return Stage::source;
  if (stage == OldStage::slice_guard) return Stage::construction_guard;
  if (stage == OldStage::slice_operation) return Stage::construction_operation;
  return Stage::construction_guard;
}

[[gnu::noinline]] auto prepare(D& d, Request& request, const L& limits, R& row)
    -> bool {
  // This EMPTY Endpoint03 carrier survives S64 and G21/O78. It is diagnostic
  // scratch only: no old ProgramKey, Context, token or bounded report is used.
  BoardingIntermediateEndpoint03Diagnostic old(d.source);
  detail::BoardingIntermediateEndpoint03Limits old_limits;
  old_limits.source_guards = limits.source_guards;
  old_limits.construction_guards = limits.construction_guards;
  old_limits.construction_operations = limits.construction_operations;
  BoardingIntermediateEndpoint03Refusal old_reason;
  const bool enrolled = detail::intermediate_endpoint03_source_enroll(
      old, request, old_limits, old_reason);
  d.source_enrolled = enrolled && old.source_enrolled;
  const bool constructed =
      enrolled && detail::intermediate_endpoint03_construct(
                      old, request, old_limits, old_reason);
  d.source_evaluated = old.source_evaluated;
  d.slice = old.slice;
  d.work.source_guards = old.work.source_guards;
  d.work.construction_guards = old.work.construction_guards;
  d.work.construction_operations = old.work.construction_operations;
  if (!constructed) {
    // Full typed forwarding, including borrowed source metadata kept alive by
    // d.source. The separate forwarding current/pending pair is prep-only.
    const BoardingIntermediateEndpoint03Refusal forwarded = old_reason;
    d.old_preparation_refusal = forwarded;
    d.old_preparation_refusal_available = true;
    row = R{};
    row.stage = mapped_stage(old_reason.self_stage);
    row.operation = old_reason.operation;
    if (old_reason.side && *old_reason.side < 2)
      row.side = static_cast<std::uint8_t>(*old_reason.side);
    row.predicate_condition = mapped_condition(old_reason.predicate_condition);
    row.limiting_bound = old_reason.limiting_bound;
    row.limiting_available = old_reason.limiting_bound.supported;
    return stop(d, row, mapped_condition(old_reason.condition));
  }
  // The fresh bool returns and exact charged masks are prerequisites for NEW
  // issuance; no caller-supplied old report can enter this lexical scope.
  const bool ready =
      d.source_enrolled && old.cells.empty() && d.work.source_guards == 64 &&
      d.source_evaluated == all_source_operations &&
      d.work.construction_guards == 21 &&
      d.work.construction_operations == 78 &&
      d.slice.guard_attempted == all_guards &&
      d.slice.guard_written == all_guards &&
      d.slice.operation_attempted == all_operations &&
      d.slice.operation_written == all_operations && d.slice.complete &&
      d.slice.arithmetic_supported &&
      detail::intermediate_endpoint03_template_valid(request, true, d.slice.y);
  if (!ready) {
    row = R{};
    row.stage = Stage::construction_guard;
    row.operation = 20;
    return stop(d, row, W::slice_identity);
  }
  return true;
}

auto map_result(detail::BoardingRouteFootPhaseCellResult result) -> Original {
  switch (result) {
    case detail::BoardingRouteFootPhaseCellResult::accepted:
      return Original::accepted;
    case detail::BoardingRouteFootPhaseCellResult::unresolved:
      return Original::unresolved;
    case detail::BoardingRouteFootPhaseCellResult::capacity:
      return Original::capacity;
    case detail::BoardingRouteFootPhaseCellResult::unsupported:
      return Original::unsupported;
  }
  return Original::not_run;
}

auto complete_leg(const BoardingRouteFootPhaseLeg& leg) -> bool {
  return leg.nominal_links && leg.target_sole_identity && leg.joint_sectors &&
         leg.derivative_domains && leg.timing_complete;
}

auto body_stop(const D& d, const Cell& cell) -> bool {
  return d.original_result == Original::capacity &&
         d.original_reason.condition == PhaseW::body_capacity &&
         !d.original_reason.side && d.work.phase.graphs == 1 &&
         d.work.phase.legs == 2 && d.work.phase.bodies == 0 &&
         d.work.phase.sectors == 2 && d.work.phase.timing == 2 &&
         complete_leg(cell.legs[0]) && complete_leg(cell.legs[1]);
}

auto anchors(const Context& x, const Token& token, const D& d) -> bool {
  return A::valid(d.source) && x.owner() == &d && x.data() &&
         x.data() == A::data(d.source) && x.request() && x.key() && x.fresh() &&
         token.context() == &x && token.owner() == &d &&
         token.request() == x.request() && token.fresh() == x.fresh() &&
         d.cells.size() == 1 && d.cells.capacity() == 1 &&
         token.cell() == &d.cells.front() && x.key()->version() == 3 &&
         d.program_version == 3 &&
         x.key()->candidate() ==
             BoardingIntermediateEndpoint03Candidate::root_y_reach_roll_slice &&
         x.key()->y() == d.slice.y && d.source_enrolled &&
         d.source_evaluated == all_source_operations &&
         d.work.source_guards == 64 && d.work.construction_guards == 21 &&
         d.work.construction_operations == 78 &&
         d.slice.guard_attempted == all_guards &&
         d.slice.guard_written == all_guards &&
         d.slice.operation_attempted == all_operations &&
         d.slice.operation_written == all_operations && d.slice.complete &&
         d.slice.arithmetic_supported &&
         detail::intermediate_endpoint03_template_valid(*x.request(), true,
                                                        x.key()->y()) &&
         d.work.phase_calls == 1 && x.fresh()->prepared && x.fresh()->invoked &&
         x.fresh()->completed && x.fresh()->calls == 1 &&
         x.fresh()->result == d.original_result;
}

auto finite_bound(const BoardingPlantedLegScalarBounds& b) -> bool {
  return std::isfinite(b.lower) && std::isfinite(b.upper) && b.lower <= b.upper;
}

[[gnu::noinline]] auto capture(const Context& x, const Token& token, D& d,
                               const L& limits, R& row) -> bool {
  const auto begin = [&](std::uint8_t cursor) {
    row = R{};
    row.stage = Stage::capture;
    row.operation = cursor;
    d.last_stage = Stage::capture;
    d.operation = d.capture_cursor = cursor;
    d.last_predicate_available = false;
    if (d.work.capture_guards >= limits.capture_guards)
      return stop(d, row, W::capture_capacity);
    ++d.work.capture_guards;
    d.capture_attempted |= static_cast<std::uint8_t>(std::uint8_t{1} << cursor);
    return true;
  };
  const auto finish = [&](bool good, W why) {
    d.capture_written |=
        static_cast<std::uint8_t>(std::uint8_t{1} << d.capture_cursor);
    d.last_predicate_available = row.predicate_available = true;
    d.last_predicate = row.predicate_value = good;
    row.predicate_condition = good ? W::none : why;
    return good || stop(d, row, why);
  };

  if (!begin(0)) return false;
  if (!finish(anchors(x, token, d), W::capture_identity)) return false;
  if (!begin(1)) return false;
  if (!finish(detail::boarding_route_foot_phase_environment(),
              W::unsupported_arithmetic))
    return false;
  if (!begin(2)) return false;
  const auto& cell = *token.cell();
  const bool joint = d.original_result == Original::unresolved &&
                     d.original_reason.condition == PhaseW::joint_sector &&
                     d.original_reason.side && *d.original_reason.side < 2;
  const bool body = !joint && body_stop(d, cell);
  d.kind = joint  ? Kind::joint_sector_refusal
           : body ? Kind::intentional_body_stop
                  : Kind::not_run;
  if (!finish(joint || body, W::original_prerequisite)) return false;
  if (!begin(3)) return false;
  const auto side = joint ? *d.original_reason.side : std::size_t{1};
  const bool side_good =
      joint
          ? d.work.phase.graphs == 1 && d.work.phase.legs == side + 1 &&
                d.work.phase.bodies == 0 && d.work.phase.sectors == side + 1 &&
                d.work.phase.timing == side &&
                d.original_result == Original::unresolved &&
                d.original_reason.condition == PhaseW::joint_sector
          : body_stop(d, cell);
  if (side_good) d.selected_side = static_cast<std::uint8_t>(side);
  if (!finish(side_good, W::capture_identity)) return false;
  if (!begin(4)) return false;
  bool margins_good = true;
  W margin_failure = W::capture_identity;
  for (std::uint8_t i = 0; i < 6; ++i) {
    const auto bit = static_cast<std::uint8_t>(std::uint8_t{1} << i);
    d.margin_read |= bit;
    const auto& margin = cell.legs[side].sector_margins[i];
    if (!finite_bound(margin)) {
      margins_good = false;
      margin_failure = W::unsupported_arithmetic;
      break;
    }
    d.sector_margins[i] = margin;
    d.margin_written |= bit;
    d.arithmetic_supported = true;
    if (margin.lower < 0) {
      if (joint) d.sector = static_cast<Sector>(i);
      margins_good = joint;
      break;
    }
  }
  // A fresh original joint_sector refusal after all six written margins is
  // the original positive-F2 branch; no F2 reconstruction is performed here.
  if (margins_good && d.margin_written == 0x3f && d.sector == Sector::not_run)
    d.sector = joint ? Sector::positive_shin : Sector::not_run;
  if (!finish(margins_good, margin_failure)) return false;
  if (!begin(5)) return false;
  bool bound_good = true;
  W bound_failure = W::capture_identity;
  if (joint) {
    const auto& bound = d.original_reason.limiting_bound;
    if (!finite_bound(bound)) {
      bound_good = false;
      bound_failure = W::unsupported_arithmetic;
    } else if (d.sector == Sector::positive_shin) {
      bound_good =
          d.margin_read == 0x3f && d.margin_written == 0x3f && bound.lower <= 0;
      for (const auto& margin : d.sector_margins)
        bound_good = bound_good && margin.lower >= 0;
    } else {
      const auto ordinal = static_cast<std::uint8_t>(d.sector);
      bound_good = ordinal < 6;
      const auto prefix = bound_good
                              ? static_cast<std::uint8_t>(
                                    (std::uint8_t{1} << (ordinal + 1)) - 1)
                              : std::uint8_t{};
      bound_good =
          bound_good && d.margin_read == prefix && d.margin_written == prefix;
      if (bound_good) {
        const auto& selected = d.sector_margins[ordinal];
        bound_good = selected.lower < 0 && selected.lower == bound.lower &&
                     selected.upper == bound.upper;
        for (std::uint8_t i = 0; i < ordinal; ++i)
          bound_good = bound_good && d.sector_margins[i].lower >= 0;
      }
    }
    if (bound_good) {
      d.selected_limiting_bound = {bound.lower, bound.upper, true};
      d.selected_limiting_available = true;
    }
  } else {
    bound_good = body_stop(d, cell) && d.margin_read == 0x3f &&
                 d.margin_written == 0x3f && d.sector == Sector::not_run;
    for (const auto& margin : d.sector_margins)
      bound_good = bound_good && margin.lower >= 0;
  }
  if (!finish(bound_good, bound_failure)) return false;
  if (!begin(6)) return false;
  if (joint) {
    const bool strict = d.sector == Sector::positive_shin
                            ? d.selected_limiting_bound.upper <= 0
                            : d.selected_limiting_bound.upper < 0;
    d.classification = strict ? Classification::strict_necessary_violation
                              : Classification::interval_inconclusive;
  } else {
    d.classification = Classification::original_sectors_complete;
  }
  d.classification_evaluated = true;
  if (!finish(true, W::capture_identity)) return false;
  if (!begin(7)) return false;
  bool consistent = anchors(x, token, d) && d.capture_written == 0x7f &&
                    d.classification_evaluated && d.arithmetic_supported &&
                    d.selected_side == side &&
                    d.margin_read == d.margin_written;
  if (joint) {
    consistent = consistent && d.kind == Kind::joint_sector_refusal &&
                 d.original_result == Original::unresolved &&
                 d.original_reason.condition == PhaseW::joint_sector &&
                 d.original_reason.side && *d.original_reason.side == side &&
                 d.work.phase.graphs == 1 && d.work.phase.legs == side + 1 &&
                 d.work.phase.bodies == 0 && d.work.phase.sectors == side + 1 &&
                 d.work.phase.timing == side && d.selected_limiting_available &&
                 d.selected_limiting_bound.supported &&
                 finite_bound(d.original_reason.limiting_bound) &&
                 d.selected_limiting_bound.lower ==
                     d.original_reason.limiting_bound.lower &&
                 d.selected_limiting_bound.upper ==
                     d.original_reason.limiting_bound.upper;
    if (d.sector == Sector::positive_shin) {
      consistent = consistent && d.margin_read == 0x3f &&
                   d.margin_written == 0x3f &&
                   d.selected_limiting_bound.lower <= 0;
      for (const auto& margin : d.sector_margins)
        consistent = consistent && margin.lower >= 0;
    } else {
      const auto ordinal = static_cast<std::uint8_t>(d.sector);
      consistent = consistent && ordinal < 6;
      if (ordinal < 6) {
        const auto prefix =
            static_cast<std::uint8_t>((std::uint8_t{1} << (ordinal + 1)) - 1);
        const auto& selected = d.sector_margins[ordinal];
        consistent = consistent && d.margin_written == prefix &&
                     selected.lower < 0 &&
                     selected.lower == d.selected_limiting_bound.lower &&
                     selected.upper == d.selected_limiting_bound.upper;
        for (std::uint8_t i = 0; i < ordinal; ++i)
          consistent = consistent && d.sector_margins[i].lower >= 0;
      }
    }
    const bool strict = d.sector == Sector::positive_shin
                            ? d.selected_limiting_bound.upper <= 0
                            : d.selected_limiting_bound.upper < 0;
    consistent =
        consistent &&
        d.classification == (strict ? Classification::strict_necessary_violation
                                    : Classification::interval_inconclusive);
  } else {
    consistent = consistent && d.kind == Kind::intentional_body_stop &&
                 body_stop(d, cell) && d.margin_written == 0x3f &&
                 d.sector == Sector::not_run &&
                 !d.selected_limiting_available &&
                 d.classification == Classification::original_sectors_complete;
    for (const auto& margin : d.sector_margins)
      consistent = consistent && margin.lower >= 0;
  }
  if (!finish(consistent, W::capture_identity)) return false;
  d.evidence_complete = true;
  d.last_stage = Stage::complete;
  return true;
}
} // namespace

auto detail::intermediate_sector_evidence02_current_call(const Context& context,
                                                         D& d, Cell& cell,
                                                         Fresh& fresh)
    -> std::optional<Token> {
  // Fixed bodies0 below structurally prevents phase_body even if both legs
  // pass. Only this fresh-purpose adapter can construct a token after recording
  // this synchronous original call. C01 later revalidates the charged tuple.
  if (context.owner() != &d || !context.request() || !context.key() ||
      context.fresh() != &fresh || !fresh.prepared || fresh.invoked ||
      fresh.calls != 0 || context.data() != A::data(d.source) ||
      !A::valid(d.source) || d.cells.size() != 1 || d.cells.capacity() != 1 ||
      &d.cells.front() != &cell)
    return {};
  detail::BoardingRouteFootPhaseLimits caps;
  caps.graphs = 1;
  caps.legs = 2;
  caps.bodies = 0;
  caps.sectors = 3;
  caps.timing = 6;
  BoardingRouteFootPhaseRefusal original;
  fresh.invoked = true;
  ++fresh.calls;
  ++d.work.phase_calls;
  d.last_stage = Stage::phase;
  d.operation = 255;
  const auto result = detail::boarding_route_foot_phase_cell(
      *context.request(), 0, 1, false, caps, d.work.phase, cell, original);
  d.original_result = fresh.result = map_result(result);
  d.original_reason = original;
  fresh.completed = true;
  d.state = d.original_result == Original::unsupported ? S::unsupported
            : d.original_result == Original::capacity  ? S::capacity
                                                       : S::unresolved;
  if (d.original_result == Original::unsupported)
    d.arithmetic_supported = false;
  return Token(context, d, cell, *context.request(), fresh);
}

[[gnu::noinline]] auto detail::intermediate_sector_evidence02_bounded(
    const OriginBoardingIntermediatePauseSupport& provider, L limits) -> E {
  if (!detail::boarding_route_foot_phase_environment())
    return std::unexpected(
        "intermediate sector evidence02 unsupported floating point");
  if (!valid_limits(limits))
    return std::unexpected("intermediate sector evidence02 invalid limits");
  if (limits.output_bytes < fixed_output)
    return std::unexpected("intermediate sector evidence02 output headers");
  E result(std::in_place, provider);
  auto& d = *result;
  d.work.preflight_guards = 1;
  d.output_capacity_bytes = fixed_output;
  d.required_output_bytes =
      boarding_intermediate_sector_evidence02_required_output_bytes();
  if (limits.output_bytes < d.required_output_bytes) {
    R room;
    room.stage = Stage::output;
    stop(d, room, W::output_capacity);
    return result;
  }
  Request request;
  R row;
  if (!prepare(d, request, limits, row)) return result;
  const BoardingIntermediateSectorEvidence02ProgramKey key(d.slice.y);
  d.reporting_elapsed_seconds = 2;
  Fresh fresh;
  fresh.prepared = true;
  const Context context(d, request, key, fresh);
  try {
    d.cells.reserve(1);
    if (d.cells.capacity() >
            (std::numeric_limits<std::size_t>::max() - fixed_output) /
                sizeof(Cell) ||
        d.cells.capacity() != 1) {
      std::vector<Cell>().swap(d.cells);
      row = R{};
      row.stage = Stage::output;
      stop(d, row, W::output_capacity);
      return result;
    }
    d.output_capacity_bytes = fixed_output + d.cells.capacity() * sizeof(Cell);
    d.cells.emplace_back();
  } catch (const std::bad_alloc&) {
    d.output_capacity_bytes = fixed_output + d.cells.capacity() * sizeof(Cell);
    row = R{};
    row.stage = Stage::output;
    stop(d, row, W::output_capacity);
    return result;
  }
  const auto token = intermediate_sector_evidence02_current_call(
      context, d, d.cells.front(), fresh);
  if (!token) {
    row = R{};
    row.stage = Stage::phase;
    stop(d, row, W::invalid_binding);
    return result;
  }
  capture(context, *token, d, limits, row);
  return result;
}

auto assess_origin_boarding_intermediate_sector_evidence02(
    const OriginBoardingIntermediatePauseSupport& provider) -> E {
  return detail::intermediate_sector_evidence02_bounded(provider);
}
} // namespace apsis_drift
