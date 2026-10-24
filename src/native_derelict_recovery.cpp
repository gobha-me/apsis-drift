#include "apsis_drift/native_flight_session.hpp"

#include "apsis_drift/freedom_derelict_recovery.hpp"
#include "apsis_drift/save_file.hpp"

#include <limits>
#include <memory>

namespace apsis_drift {
namespace {
auto retrieval_observer(const NativeFreedomFlightSession& session)
    -> std::optional<FreedomDerelictObservation> {
  const auto context = session.derelict_view();
  if (!context.local || !context.inspected || !context.can_inspect)
    return std::nullopt;
  const auto body = session.relative_state();
  if (!body) return std::nullopt;
  return FreedomDerelictObservation{session.document().origin.state.craft,
                                    body->tick, body->position_metres,
                                    body->linear_velocity_metres_per_second};
}
} // namespace

auto NativeFreedomFlightSession::derelict_recovery_view() const
    -> std::expected<NativeDerelictRecoveryView, std::string> {
  NativeDerelictRecoveryView view;
  view.tick = document_.flight.tick;
  view.selected = knowledge_ && knowledge_->derelict_recovery.has_value();
  view.note = "This journey has no derelict recovery prototype selected";
  if (!view.selected) return view;
  const auto& state = *knowledge_->derelict_recovery;
  view.known = knowledge_->derelicts && knowledge_->derelicts->contact;
  if (state.canister) {
    view.acquired_ticks = kFreedomDerelictRetrievalTicks;
    switch (state.canister->disposition) {
      case FreedomCanisterDisposition::carried:
        view.status = "carried";
        view.note = "One sealed service canister carried. Review its sale at "
                    "Origin; the source is depleted.";
        break;
      case FreedomCanisterDisposition::lost:
        view.status = "lost";
        view.note = "The recovered canister was lost. Its recovery note "
                    "remains and the source is depleted.";
        break;
      case FreedomCanisterDisposition::sold:
        view.status = "sold";
        view.note = "Canister sold. The recovery note and earnings remain; the "
                    "source is depleted.";
        break;
    }
    return view;
  }
  view.active = state.active.has_value();
  if (state.active) {
    view.status = "retrieving";
    view.acquired_ticks = state.active->acquired_ticks;
  }
  view.note = "Inspect the local derelict exterior before retrieving its "
              "sealed service canister.";
  const auto observer = retrieval_observer(*this);
  if (!observer) return view;
  const auto assessed = assess_freedom_derelict_retrieval(
      document_.origin.recipe.universe_seed, *observer);
  if (!assessed) return std::unexpected{assessed.error()};
  view.note = "Hold 100–150 m from the inspected hull at relative speed at "
              "most 2 m/s for 30 seconds. No autopilot or transfer animation.";
  if (!assessed->eligible) return view;
  if (view.active) {
    view.note = state.active->last_tick == view.tick
                    ? "Retrieving sealed canister. Maintain stand-off and "
                      "matched speed; ordinary fuel and time continue."
                    : "Retrieval clock gap: the next ordinary flight tick "
                      "cancels this attempt; begin again afterward.";
  } else if (view.tick <= std::numeric_limits<SimulationTick>::max() - 1 -
                              kFreedomDerelictRetrievalTicks) {
    view.can_begin = true;
  } else {
    view.note =
        "The remaining saved clock cannot contain a complete retrieval.";
  }
  return view;
}

auto NativeFreedomFlightSession::begin_derelict_recovery()
    -> std::expected<void, std::string> {
  const auto view = derelict_recovery_view();
  if (!view) return std::unexpected{view.error()};
  if (!view->can_begin) return std::unexpected{view->note};
  const auto observer = retrieval_observer(*this);
  if (!observer) return std::unexpected{"The retrieval flight context changed"};
  const auto next = begin_freedom_derelict_retrieval(
      document_.origin.recipe.universe_seed, *knowledge_->derelict_recovery,
      *knowledge_->derelicts, *observer);
  if (!next) return std::unexpected{next.error()};
  auto candidate = std::make_unique<NativeFreedomFlightSession>(*this);
  candidate->knowledge_->derelict_recovery = *next;
  const auto saved = candidate->save_document();
  if (!saved) return std::unexpected{saved.error()};
  if (auto encoded = encode_native_save_document_json(*saved); !encoded)
    return std::unexpected{encoded.error().detail};
  *this = std::move(*candidate);
  return {};
}

auto NativeFreedomFlightSession::cancel_derelict_recovery_unfinished() noexcept
    -> void {
  if (knowledge_ && knowledge_->derelict_recovery)
    knowledge_->derelict_recovery =
        cancel_freedom_derelict_retrieval(*knowledge_->derelict_recovery);
}

auto NativeFreedomFlightSession::cancel_derelict_recovery()
    -> std::expected<void, std::string> {
  if (recovery_pending()) return std::unexpected{"Recovery is pending"};
  if (!knowledge_ || !knowledge_->derelict_recovery ||
      !knowledge_->derelict_recovery->active)
    return std::unexpected{"No unfinished canister retrieval"};
  auto candidate = std::make_unique<NativeFreedomFlightSession>(*this);
  candidate->cancel_derelict_recovery_unfinished();
  const auto saved = candidate->save_document();
  if (!saved) return std::unexpected{saved.error()};
  if (auto encoded = encode_native_save_document_json(*saved); !encoded)
    return std::unexpected{encoded.error().detail};
  *this = std::move(*candidate);
  return {};
}

auto NativeFreedomFlightSession::advance_derelict_recovery_tick()
    -> std::expected<void, std::string> {
  if (recovery_pending() || !knowledge_ || !knowledge_->derelict_recovery ||
      !knowledge_->derelict_recovery->active)
    return {};
  const auto observer = retrieval_observer(*this);
  if (!observer) {
    cancel_derelict_recovery_unfinished();
    return {};
  }
  const auto next = advance_freedom_derelict_retrieval(
      document_.origin.recipe.universe_seed, *knowledge_->derelict_recovery,
      *knowledge_->derelicts, *observer);
  if (!next) return std::unexpected{next.error()};
  knowledge_->derelict_recovery = *next;
  return {};
}

auto NativeFreedomFlightSession::derelict_recovery_sale_token() const
    -> std::string {
  std::string token = "canister1:" + paid_operations_token();
  if (knowledge_ && knowledge_->derelict_recovery &&
      knowledge_->derelict_recovery->canister) {
    const auto& item = *knowledge_->derelict_recovery->canister;
    token += ':' + std::to_string(item.started_tick) + ':' +
             std::to_string(item.completed.craft.value) + ':' +
             std::to_string(item.completed.tick) + ':' +
             std::to_string(static_cast<unsigned>(item.disposition));
  }
  return token;
}

auto NativeFreedomFlightSession::derelict_recovery_sale_view() const
    -> std::expected<NativeDerelictRecoverySaleView, std::string> {
  NativeDerelictRecoverySaleView view;
  view.tick = document_.flight.tick;
  view.note = "This journey has no derelict recovery prototype selected.";
  if (!knowledge_ || !resources_ || !knowledge_->derelict_recovery) return view;
  view.supported = true;
  view.at_origin = paid_operations_origin_available();
  view.paid_selected = knowledge_->paid_operations.has_value();
  view.earned_credits =
      paid_income_counts().canister_sold * kFreedomDerelictCanisterCredits;
  view.token = derelict_recovery_sale_token();
  const auto& item = knowledge_->derelict_recovery->canister;
  if (item && item->disposition == FreedomCanisterDisposition::carried) {
    view.carried_count = 1;
    view.contents =
        "Sealed service canister · recovered from the inspected derelict";
  }
  view.gross_credits = view.carried_count * kFreedomDerelictCanisterCredits;
  view.net_credits = view.gross_credits;
  if (!view.at_origin) {
    view.note = "Physically attach at Origin to sell the carried canister. "
                "Retained recovery notes alone cannot be sold.";
    return view;
  }
  if (!view.carried_count) {
    view.note = "No physical canister is carried. A recovered source stays "
                "depleted after sale or loss.";
    return view;
  }
  auto counts = paid_income_counts();
  if (counts.canister_sold)
    return std::unexpected{"Canister sale is already recorded"};
  counts.canister_sold = 1;
  if (knowledge_->paid_operations) {
    const auto before = effective_paid_operations_account();
    if (!before) return std::unexpected{before.error()};
    const auto after =
        settle_paid_canister_income(*knowledge_->paid_operations, counts,
                                    view.tick, paid_operations_generation());
    if (!after) return std::unexpected{after.error()};
    view.insurance_credits = before->recovery_due - after->recovery_due;
    view.tab_credits = before->operating_tab - after->operating_tab;
    if (view.insurance_credits + view.tab_credits > view.gross_credits)
      return std::unexpected{"Canister deductions exceed its quote"};
    view.net_credits =
        view.gross_credits - view.insurance_credits - view.tab_credits;
    const auto cash = paid_operations_cash(*after, counts);
    if (!cash) return std::unexpected{cash.error()};
    view.cash_after = *cash;
  }
  view.can_sell = true;
  view.note = "Provisional price: 100 credits for the carried sealed canister. "
              "Insurance is settled first, then up to half the remainder pays "
              "the operating tab. The recovery note remains. No sale or paid "
              "enrollment happens on docking, reading or Continue.";
  return view;
}

auto NativeFreedomFlightSession::sell_derelict_recovery(std::string_view token)
    -> std::expected<void, std::string> {
  const auto quote = derelict_recovery_sale_view();
  if (!quote) return std::unexpected{quote.error()};
  if (!quote->can_sell || token != quote->token)
    return std::unexpected{"Review the current canister quote at Origin"};
  if (knowledge_->derelict_recovery->canister->completed.craft !=
      document_.origin.state.craft)
    return std::unexpected{"The canister belongs to a different carrier"};
  const auto sold = sell_freedom_derelict_canister(
      document_.origin.recipe.universe_seed, *knowledge_->derelict_recovery,
      *knowledge_->derelicts, document_.flight.tick);
  if (!sold) return std::unexpected{sold.error()};
  auto candidate = std::make_unique<NativeFreedomFlightSession>(*this);
  candidate->knowledge_->derelict_recovery = *sold;
  if (knowledge_->paid_operations) {
    auto counts = paid_income_counts();
    counts.canister_sold = 1;
    const auto paid = settle_paid_canister_income(*knowledge_->paid_operations,
                                                  counts, document_.flight.tick,
                                                  paid_operations_generation());
    if (!paid) return std::unexpected{paid.error()};
    candidate->knowledge_->paid_operations = *paid;
  }
  const auto saved = candidate->save_document();
  if (!saved) return std::unexpected{saved.error()};
  if (auto encoded = encode_native_save_document_json(*saved); !encoded)
    return std::unexpected{encoded.error().detail};
  *this = std::move(*candidate);
  return {};
}
} // namespace apsis_drift
